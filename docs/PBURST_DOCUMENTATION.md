# Documentación Técnica: PBURST (RMT Pulse Burst Capture)

## Tabla de Contenidos

1. [Introducción](#introducción)
2. [Arquitectura del Sistema](#arquitectura-del-sistema)
3. [Flujo de Datos](#flujo-de-datos)
4. [Interpretación de Campos](#interpretación-de-campos)
5. [Precisiones y Limitaciones](#precisiones-y-limitaciones)
6. [Configuraciones Disponibles](#configuraciones-disponibles)
7. [Ejemplos con Diferentes Configuraciones](#ejemplos-con-diferentes-configuraciones)
8. [Cómo Cambiar las Configuraciones](#cómo-cambiar-las-configuraciones)
9. [Troubleshooting](#troubleshooting)

---

## Introducción

**PBURST** es el sistema de captura de pulsos de alta precisión basado en el periférico **RMT (Remote Control)** del ESP32. Este sistema está diseñado para detectar y medir con precisión de microsegundos trenes de pulsos eléctricos provenientes de detectores de rayos cósmicos.

### Características Principales

- **Alta precisión temporal**: Resolución de 500ns (2MHz) para medición de duración y separación
- **Múltiples canales**: Captura simultánea en 3 canales independientes (ch1, ch2, ch3)
- **Timestamp Unix con microsegundos**: Cada grupo de pulsos incluye un timestamp absoluto con precisión de microsegundos
- **Detección de grupos**: Agrupa pulsos consecutivos en "bursts" o trenes de pulsos
- **Filtrado de glitches**: Filtro configurable para eliminar ruido y pulsos espurios

### Propósito

El sistema está diseñado para:
- Capturar pulsos de detectores de muones y otras partículas
- Medir duraciones de pulsos con precisión de microsegundos
- Calcular periodos entre pulsos (separación)
- Proporcionar timestamps absolutos para análisis de coincidencias entre canales
- Publicar datos estructurados vía MQTT para análisis posterior

---

## Arquitectura del Sistema

### Componentes Principales

```
┌─────────────────────────────────────────────────────────────┐
│                    Hardware Layer                           │
│  GPIO Pins: PIN_PULSE_IN_CH1, CH2, CH3                     │
│  RMT Peripheral: 3 canales RX independientes                │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│              RMT Driver (ESP-IDF)                            │
│  - Resolución: 2MHz (500ns por tick)                        │
│  - Buffer: 64 símbolos por canal                            │
│  - Filtro de glitches: CONFIG_RMT_GLITCH_FILTER_NS          │
│  - Rango máximo: 10ms por pulso                              │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│         ISR Callback (rmt_rx_done_callback)                 │
│  - Se ejecuta cuando el buffer se llena o hay timeout       │
│  - Procesa símbolos RMT en tiempo real                      │
│  - Detecta pulsos (HIGH→LOW)                                │
│  - Calcula duraciones y separaciones                        │
│  - Envía grupos de pulsos a cola (ISR-safe)                 │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│      Cola de Grupos (rmt_group_queue)                       │
│  - Tamaño: 10 grupos                                        │
│  - Almacena punteros a estructuras rmt_pulse_group          │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│    Tarea de Procesamiento (task_rmt_event_processor)        │
│  - Core: 1 (dedicado)                                       │
│  - Prioridad: 3                                             │
│  - Convierte timestamps de boot a Unix                      │
│  - Crea mensajes de telemetría                              │
│  - Reinicia captura RMT después de cada callback            │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│         Cola de Telemetría (telemetry_queue)                 │
│  - Mensajes tipo TM_RMT_PULSE_EVENT                         │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│          MQTT Publisher (mss_sender)                        │
│  - Serializa a JSON                                         │
│  - Publica en topic: {station}/{experiment}/{device}/pburst │
└─────────────────────────────────────────────────────────────┘
```

### Estructuras de Datos

#### `rmt_pulse_t` (Pulso Individual)
```c
typedef struct {
    uint32_t duration_us;   // Duración del pulso en microsegundos
    int64_t separation_us;  // Separación con pulso anterior (μs, -1 si es el primero)
} rmt_pulse_t;
```

#### `rmt_pulse_group` (Grupo de Pulsos)
```c
struct rmt_pulse_group {
    uint8_t channel_index;      // Índice interno (0, 1, 2)
    uint8_t num_pulses;          // Número de pulsos en el grupo
    int64_t start_timestamp;     // Timestamp del primer pulso (μs desde boot)
    rmt_pulse_t pulses[];        // Array flexible de pulsos
};
```

#### Mensaje MQTT JSON
```json
{
  "start_datetime": "1703764800123456",
  "channel": "ch3",
  "symbols": 3,
  "pulses": [
    {
      "duration_us": 6,
      "separation_us": -1
    },
    {
      "duration_us": 6,
      "separation_us": 60
    },
    {
      "duration_us": 6,
      "separation_us": 60
    }
  ]
}
```

---

## Flujo de Datos

### 1. Captura Hardware (RMT)

El periférico RMT captura transiciones de nivel en los pines GPIO configurados. Cada transición se almacena como un **símbolo RMT** con:
- `level0`: Nivel inicial (0=LOW, 1=HIGH)
- `level1`: Nivel final después de la transición
- `duration0`: Duración en ticks del nivel inicial
- `duration1`: Duración en ticks del nivel final

**Resolución actual**: 2MHz = 500ns por tick = 2 ticks por microsegundo

### 2. Procesamiento en ISR

Cuando el buffer RMT se llena (64 símbolos) o se alcanza un timeout, se invoca `rmt_rx_done_callback`:

1. **Conteo de pulsos**: Primera pasada para contar cuántos pulsos hay (símbolos con `level0=1, level1=0`)
2. **Cálculo de timestamps**: Se calcula el tiempo de inicio del primer símbolo trabajando hacia atrás desde el tiempo del callback
3. **Procesamiento de símbolos**: Segunda pasada procesando símbolos en orden cronológico:
   - Detecta pulsos (HIGH→LOW)
   - Calcula `duration_us` = duración del nivel HIGH
   - Calcula `separation_us` = periodo desde inicio del pulso anterior
4. **Agrupación**: Todos los pulsos de un callback forman un grupo
5. **Envío a cola**: El grupo se envía a `rmt_group_queue` (ISR-safe)

### 3. Conversión de Timestamps

En la tarea de procesamiento (`task_rmt_event_processor`):

1. Se obtiene el tiempo Unix actual con `gettimeofday()`
2. Se obtiene el tiempo desde boot actual con `esp_timer_get_time()`
3. Se calcula el timestamp Unix del evento:
   ```
   unix_timestamp = current_unix_time - (current_boot_time - event_boot_time)
   ```

Esto convierte el timestamp relativo (desde boot) a un timestamp absoluto Unix con precisión de microsegundos.

### 4. Publicación MQTT

El mensaje se serializa a JSON y se publica en el topic:
```
{station}/{experiment}/{device}/pburst
```

---

## Interpretación de Campos

### `start_datetime`

- **Tipo**: String (número entero)
- **Formato**: Timestamp Unix en microsegundos
- **Precisión**: 1 microsegundo
- **Significado**: Momento absoluto del inicio del primer pulso del grupo
- **Ejemplo**: `"1703764800123456"` = 2023-12-28 12:00:00.123456 UTC (aproximadamente)

**Uso**:
- Referencia temporal absoluta para sincronización entre canales
- Análisis de coincidencias entre diferentes detectores
- Correlación con otros sistemas de medición

**Cálculo del tiempo absoluto de cada pulso**:
- Pulso 1: `start_datetime` (directo)
- Pulso 2: `start_datetime + pulses[1].separation_us` (si `pulses[0].separation_us == -1`)
- Pulso 3: `start_datetime + pulses[1].separation_us + pulses[2].separation_us`
- Pulso i: `start_datetime + sum(separation_us[j] for j in [1..i])` (ignorando -1)

### `duration_us`

- **Tipo**: Number (entero, sin signo)
- **Unidad**: Microsegundos
- **Precisión**: 0.5μs (resolución de 500ns, redondeada a microsegundos)
- **Significado**: Duración del pulso en nivel HIGH
- **No incluye**: El tiempo en LOW después del pulso

**Ejemplo**:
```
Señal:     ──┐──┐──────
            HIGH LOW
            ↑    ↑
          Inicio Fin
          
duration_us = 6 (tiempo en HIGH)
```

**Limitaciones**:
- Mínimo: `CONFIG_RMT_GLITCH_FILTER_NS / 1000` (por defecto 1.3μs)
- Máximo: 10,000,000ns = 10ms (configurado en código)

### `separation_us`

- **Tipo**: Number (entero con signo)
- **Unidad**: Microsegundos
- **Precisión**: 0.5μs (resolución de 500ns, redondeada a microsegundos)
- **Significado**: **Periodo** entre pulsos (tiempo desde inicio del pulso anterior hasta inicio del pulso actual)
- **Valores especiales**:
  - `-1`: Primer pulso del grupo (o no hay pulso anterior conocido en el mismo grupo)
  - `> 0`: Periodo desde inicio del pulso anterior

**Importante**: `separation_us` mide el **periodo** (start-to-start), NO el gap (end-to-start).

**Ejemplo**:
```
Tiempo:    0μs    6μs   60μs   66μs  120μs
Señal:     ──┐──┐──────┐──┐──────┐──┐
            P1  P2      P3
            ↑   ↑       ↑
          start start  start
          
Pulso 1: separation_us = -1 (primer pulso)
Pulso 2: separation_us = 60 (periodo: 60μs desde inicio de P1)
Pulso 3: separation_us = 60 (periodo: 60μs desde inicio de P2)
```

**Cálculo del gap (tiempo en LOW)**:
```
gap_us = separation_us - duration_us
```

En el ejemplo anterior:
- Gap entre P1 y P2: `60 - 6 = 54μs`
- Gap entre P2 y P3: `60 - 6 = 54μs`

### `channel`

- **Tipo**: String
- **Valores**: `"ch1"`, `"ch2"`, `"ch3"`
- **Significado**: Identificador del canal físico donde se capturó el grupo de pulsos
- **Mapeo**: 
  - `ch1` = GPIO `PIN_PULSE_IN_CH1` (canal RMT interno: 0)
  - `ch2` = GPIO `PIN_PULSE_IN_CH2` (canal RMT interno: 1)
  - `ch3` = GPIO `PIN_PULSE_IN_CH3` (canal RMT interno: 2)

### `symbols`

- **Tipo**: Number (entero, sin signo)
- **Significado**: Número de pulsos en el grupo (igual a `pulses.length`)
- **Rango**: 1 a 64 (limitado por el tamaño del buffer RMT)

---

## Precisiones y Limitaciones

### Precisión Temporal

#### Resolución del RMT
- **Configuración actual**: 2MHz = 500ns por tick
- **Precisión de medición**: ±0.5μs (1 tick)
- **Razón**: Se usa 2MHz en lugar de 80MHz para permitir capturar pulsos más largos (hasta 32.7ms vs 819μs)

#### Precisión de Timestamps
- **Timestamp Unix**: Precisión de microsegundos (1μs)
- **Fuente**: `gettimeofday()` + `esp_timer_get_time()`
- **Deriva**: Depende de la precisión del reloj del sistema (típicamente <1ms/día con NTP)

### Limitaciones de Hardware

#### Duración de Pulso
- **Mínimo**: `CONFIG_RMT_GLITCH_FILTER_NS` (por defecto 1.3μs)
  - Pulsos más cortos se filtran como glitches
- **Máximo**: 10,000,000ns = 10ms
  - Configurado en `signal_range_max_ns`
  - Límite teórico con 2MHz: 32.7ms (65535 ticks)

#### Separación entre Pulsos
- **Mínimo**: Limitado por la resolución (0.5μs)
- **Máximo**: Prácticamente ilimitado (depende del timeout)
  - Timeout configurado: `CONFIG_RMT_RX_TIMEOUT_US` (por defecto 1000μs)

#### Tamaño de Grupo
- **Máximo**: 64 pulsos por grupo
  - Limitado por `RMT_RX_BUFFER_SIZE` (64 símbolos)
  - Si hay más de 64 pulsos, se dividen en múltiples grupos

### Limitaciones de Software

#### Cola de Grupos
- **Tamaño**: 10 grupos
- **Consecuencia**: Si la cola se llena, se pierden grupos (se registra un warning en logs)

#### Memoria
- **Asignación dinámica**: Cada grupo se asigna en memoria interna
- **Liberación**: La memoria se libera después de publicar en MQTT
- **Riesgo**: Si MQTT falla repetidamente, puede haber fuga de memoria

#### Rate Limiting de Logs
- **Límite**: Máximo 3 mensajes por segundo por canal
- **Razón**: Evitar saturar los logs en alta frecuencia de eventos

---

## Configuraciones Disponibles

Todas las configuraciones se realizan mediante `menuconfig` (ESP-IDF):

```bash
idf.py menuconfig
```

Navegar a: `Component config → NMDA ORCA NEMO → RMT Pulse Detection`

### `ENABLE_RMT_PULSE_DETECTION`

- **Tipo**: Boolean
- **Default**: `n` (deshabilitado)
- **Descripción**: Habilita/deshabilita completamente el sistema RMT
- **Efecto**: Si está deshabilitado, todo el código RMT se excluye de la compilación

### `RMT_GLITCH_FILTER_NS`

- **Tipo**: Integer
- **Default**: `1300` (1.3μs)
- **Rango**: 0 - 10000 nanosegundos
- **Descripción**: Filtro de glitches para eliminar ruido y pulsos espurios
- **Efecto**: Pulsos más cortos que este valor se ignoran
- **Recomendaciones**:
  - **0**: Deshabilitar filtro (no recomendado, puede capturar ruido)
  - **500-1000ns**: Para pulsos muy cortos (p. ej., <2μs)
  - **1300ns (default)**: Balance entre filtrado y sensibilidad
  - **5000-10000ns**: Para entornos muy ruidosos o pulsos más largos

### `RMT_RX_TIMEOUT_US`

- **Tipo**: Integer
- **Default**: `1000` (1ms)
- **Rango**: 10 - 10000 microsegundos
- **Descripción**: Timeout para detectar el fin de un pulso
- **Efecto**: Si no hay transición en este tiempo, se considera que el pulso terminó
- **Recomendaciones**:
  - **100-500μs**: Para pulsos rápidos y alta frecuencia
  - **1000μs (default)**: Balance general
  - **5000-10000μs**: Para pulsos muy largos o baja frecuencia

### `RMT_COINCIDENCE_TOLERANCE_US`

- **Tipo**: Integer
- **Default**: `10` microsegundos
- **Rango**: 1 - 1000 microsegundos
- **Descripción**: Ventana de tiempo para detectar coincidencias entre canales
- **Efecto**: Pulses de diferentes canales dentro de esta ventana se consideran coincidentes
- **Nota**: Esta configuración afecta a la detección de coincidencias (no implementada en la versión actual de pburst)

### `RMT_MULTIPLICITY_THRESHOLD_US`

- **Tipo**: Integer
- **Default**: `100` microsegundos
- **Rango**: 1 - 10000 microsegundos
- **Descripción**: Umbral máximo de separación para agrupar pulsos en multiplicidad
- **Efecto**: Pulsos en el mismo canal con separación menor se agrupan
- **Nota**: Esta configuración afecta a la detección de multiplicidad (no implementada en la versión actual de pburst)

### `RMT_EVENT_BUFFER_SIZE`

- **Tipo**: Integer
- **Default**: `100` eventos
- **Rango**: 10 - 1000 eventos
- **Descripción**: Tamaño del buffer circular para almacenar eventos por canal
- **Efecto**: Buffers más grandes permiten mayor frecuencia de eventos pero consumen más memoria
- **Nota**: Esta configuración no se usa actualmente en pburst (se usa `rmt_group_queue`)

### Configuraciones Hardcodeadas (en Código)

Estas configuraciones están fijas en el código y requieren modificar el fuente para cambiarlas:

#### `resolution_hz` (Resolución RMT)
- **Valor actual**: `2000000` (2MHz)
- **Ubicación**: `main/rmt_pulse_capture.c:234`
- **Efecto**: 
  - 2MHz = 500ns por tick = 2 ticks por microsegundo
  - Permite pulsos hasta 32.7ms
- **Alternativas**:
  - `80000000` (80MHz): Mayor precisión (12.5ns) pero límite de 819μs por pulso
  - `1000000` (1MHz): Menor precisión (1μs) pero permite pulsos hasta 65.5ms

#### `mem_block_symbols` (Tamaño de Buffer RMT)
- **Valor actual**: `64` símbolos
- **Ubicación**: `main/rmt_pulse_capture.c:233`
- **Efecto**: Máximo 64 pulsos por grupo
- **Alternativas**: Puede aumentarse si hay más memoria disponible

#### `signal_range_max_ns` (Rango Máximo de Pulso)
- **Valor actual**: `10000000` (10ms)
- **Ubicación**: `main/rmt_pulse_capture.c:262, 367`
- **Efecto**: Pulsos más largos que 10ms pueden no capturarse correctamente
- **Alternativas**: Puede aumentarse hasta 32,767,500ns (32.7ms) con resolución 2MHz

#### `RMT_RX_BUFFER_SIZE` (Tamaño de Buffer de Recepción)
- **Valor actual**: `64` símbolos
- **Ubicación**: `main/rmt_pulse_capture.c:32`
- **Efecto**: Buffer para almacenar símbolos RMT antes del callback
- **Alternativas**: Puede aumentarse si hay más memoria disponible

---

## Ejemplos con Diferentes Configuraciones

### Ejemplo 1: Pulsos Rápidos (6μs, periodo 60μs)

**Configuración**:
- `RMT_GLITCH_FILTER_NS = 1300` (default)
- `RMT_RX_TIMEOUT_US = 1000` (default)
- `resolution_hz = 2000000` (2MHz)

**Señal de entrada**:
```
Tiempo:    0μs    6μs   60μs   66μs  120μs  126μs
Señal:     ──┐──┐──────┐──┐──────┐──┐
            HIGH LOW  HIGH LOW  HIGH LOW
            P1        P2        P3
```

**Mensaje MQTT**:
```json
{
  "start_datetime": "1703764800123456",
  "channel": "ch3",
  "symbols": 3,
  "pulses": [
    {
      "duration_us": 6,
      "separation_us": -1
    },
    {
      "duration_us": 6,
      "separation_us": 60
    },
    {
      "duration_us": 6,
      "separation_us": 60
    }
  ]
}
```

**Interpretación**:
- Pulso 1: Inicio en `1703764800123456` μs Unix, duración 6μs, gap 54μs
- Pulso 2: Inicio en `1703764800123516` μs Unix (3456 + 60), duración 6μs, gap 54μs
- Pulso 3: Inicio en `1703764800123576` μs Unix (3456 + 120), duración 6μs

### Ejemplo 2: Pulsos Largos (100μs, periodo 1ms)

**Configuración**:
- `RMT_GLITCH_FILTER_NS = 1300` (default)
- `RMT_RX_TIMEOUT_US = 1000` (default)
- `resolution_hz = 2000000` (2MHz)

**Señal de entrada**:
```
Tiempo:    0μs     100μs   1000μs  1100μs  2000μs  2100μs
Señal:     ──┐──────┐────────┐──────┐────────┐──────┐
            HIGH   LOW      HIGH   LOW      HIGH   LOW
            P1              P2              P3
```

**Mensaje MQTT**:
```json
{
  "start_datetime": "1703764800123456",
  "channel": "ch1",
  "symbols": 3,
  "pulses": [
    {
      "duration_us": 100,
      "separation_us": -1
    },
    {
      "duration_us": 100,
      "separation_us": 1000
    },
    {
      "duration_us": 100,
      "separation_us": 1000
    }
  ]
}
```

**Interpretación**:
- Pulso 1: Inicio en `1703764800123456` μs Unix, duración 100μs, gap 900μs
- Pulso 2: Inicio en `1703764800124456` μs Unix (3456 + 1000), duración 100μs, gap 900μs
- Pulso 3: Inicio en `1703764800125456` μs Unix (3456 + 2000), duración 100μs

### Ejemplo 3: Pulsos Muy Cortos (2μs, periodo 20μs) con Filtro Ajustado

**Configuración**:
- `RMT_GLITCH_FILTER_NS = 500` (ajustado para pulsos cortos)
- `RMT_RX_TIMEOUT_US = 500` (ajustado para alta frecuencia)
- `resolution_hz = 2000000` (2MHz)

**Señal de entrada**:
```
Tiempo:    0μs   2μs   20μs  22μs   40μs  42μs
Señal:     ─┐─┐──────┐─┐──────┐─┐──────┐
           HIGH LOW HIGH LOW HIGH LOW
           P1       P2       P3
```

**Mensaje MQTT**:
```json
{
  "start_datetime": "1703764800123456",
  "channel": "ch2",
  "symbols": 3,
  "pulses": [
    {
      "duration_us": 2,
      "separation_us": -1
    },
    {
      "duration_us": 2,
      "separation_us": 20
    },
    {
      "duration_us": 2,
      "separation_us": 20
    }
  ]
}
```

**Nota**: Con `RMT_GLITCH_FILTER_NS = 1300` (default), estos pulsos de 2μs podrían filtrarse. Con `500ns`, se capturan correctamente.

### Ejemplo 4: Pulsos Muy Largos (5ms, periodo 10ms)

**Configuración**:
- `RMT_GLITCH_FILTER_NS = 1300` (default)
- `RMT_RX_TIMEOUT_US = 10000` (aumentado para pulsos largos)
- `resolution_hz = 2000000` (2MHz)
- `signal_range_max_ns = 10000000` (10ms, debe ser >= duración del pulso)

**Señal de entrada**:
```
Tiempo:    0ms     5ms     10ms    15ms     20ms
Señal:     ──┐──────┐───────┐───────┐───────┐
            HIGH   LOW     HIGH   LOW     HIGH
            P1              P2              P3
```

**Mensaje MQTT**:
```json
{
  "start_datetime": "1703764800123456",
  "channel": "ch3",
  "symbols": 3,
  "pulses": [
    {
      "duration_us": 5000,
      "separation_us": -1
    },
    {
      "duration_us": 5000,
      "separation_us": 10000
    },
    {
      "duration_us": 5000,
      "separation_us": 10000
    }
  ]
}
```

**Nota**: Para pulsos >10ms, es necesario modificar `signal_range_max_ns` en el código fuente.

### Ejemplo 5: Grupo Grande (20 pulsos)

**Configuración**: Default

**Señal de entrada**: 20 pulsos consecutivos de 10μs con periodo de 50μs

**Mensaje MQTT**:
```json
{
  "start_datetime": "1703764800123456",
  "channel": "ch1",
  "symbols": 20,
  "pulses": [
    {"duration_us": 10, "separation_us": -1},
    {"duration_us": 10, "separation_us": 50},
    {"duration_us": 10, "separation_us": 50},
    // ... (17 pulsos más)
    {"duration_us": 10, "separation_us": 50}
  ]
}
```

**Nota**: Si hay más de 64 pulsos, se dividen en múltiples grupos (múltiples mensajes MQTT).

---

## Cómo Cambiar las Configuraciones

### Configuraciones mediante menuconfig

1. **Abrir menuconfig**:
   ```bash
   idf.py menuconfig
   ```

2. **Navegar a la sección**:
   ```
   Component config → NMDA ORCA NEMO → RMT Pulse Detection
   ```

3. **Modificar valores**:
   - Usar las flechas para navegar
   - Presionar `Enter` para editar valores numéricos
   - Presionar `Space` para alternar valores booleanos
   - Presionar `S` para guardar
   - Presionar `Q` para salir

4. **Recompilar**:
   ```bash
   idf.py build
   ```

5. **Flashear**:
   ```bash
   idf.py flash
   ```

### Configuraciones Hardcodeadas (Requieren Modificar Código)

Para cambiar configuraciones hardcodeadas, editar `main/rmt_pulse_capture.c`:

#### Cambiar Resolución RMT

**Línea 234**: Cambiar `resolution_hz`:
```c
.resolution_hz = 2000000,  // Cambiar a 80000000 para mayor precisión
```

**Efectos**:
- **80MHz**: Precisión 12.5ns, pero límite de 819μs por pulso
- **2MHz (actual)**: Precisión 500ns, permite hasta 32.7ms por pulso
- **1MHz**: Precisión 1μs, permite hasta 65.5ms por pulso

**También actualizar línea 64** (comentario/documentación):
```c
const uint32_t ticks_per_us = 2;  // Para 2MHz: 2 ticks/μs
// Para 80MHz: 80 ticks/μs
// Para 1MHz: 1 tick/μs
```

#### Cambiar Tamaño de Buffer RMT

**Línea 32**: Cambiar `RMT_RX_BUFFER_SIZE`:
```c
#define RMT_RX_BUFFER_SIZE 128  // Aumentar de 64 a 128
```

**Línea 233**: Cambiar `mem_block_symbols`:
```c
.mem_block_symbols = 128,  // Aumentar de 64 a 128
```

**Efecto**: Permite grupos de hasta 128 pulsos (en lugar de 64)

**Nota**: Asegurarse de tener suficiente memoria disponible.

#### Cambiar Rango Máximo de Pulso

**Líneas 262 y 367**: Cambiar `signal_range_max_ns`:
```c
.signal_range_max_ns = 20000000,  // Aumentar de 10ms a 20ms
```

**Efecto**: Permite capturar pulsos más largos

**Límite teórico con 2MHz**: 32,767,500ns (32.7ms)

---

## Troubleshooting

### Problema: Pulsos no se capturan

**Síntomas**: No se reciben mensajes MQTT de `pburst`

**Posibles causas**:
1. **Filtro de glitches muy alto**: `RMT_GLITCH_FILTER_NS` mayor que la duración del pulso
   - **Solución**: Reducir `RMT_GLITCH_FILTER_NS` en menuconfig

2. **RMT deshabilitado**: `ENABLE_RMT_PULSE_DETECTION = n`
   - **Solución**: Habilitar en menuconfig

3. **GPIO incorrectos**: Los pines no están conectados correctamente
   - **Solución**: Verificar `PIN_PULSE_IN_CH1/CH2/CH3` en código

4. **Pulsos fuera de rango**: Duración > 10ms
   - **Solución**: Aumentar `signal_range_max_ns` en código

### Problema: `separation_us` siempre es 0 o -1

**Síntomas**: Todos los pulsos tienen `separation_us = 0` o `-1`

**Causa**: Error en el cálculo de separación (ya corregido en versión actual)

**Solución**: Asegurarse de usar la versión más reciente del código

### Problema: Timestamps incorrectos

**Síntomas**: `start_datetime` muestra valores muy grandes o negativos

**Causa**: El sistema no tiene sincronización NTP o el reloj del sistema está desincronizado

**Solución**:
1. Verificar que el sistema tenga acceso a NTP
2. Verificar logs de sincronización de tiempo
3. Los timestamps relativos (separación) siguen siendo correctos

### Problema: Pérdida de grupos (queue full)

**Síntomas**: Logs muestran "Failed to queue pulse group: ch=X (queue full)"

**Causa**: La frecuencia de pulsos es muy alta y la cola se satura

**Soluciones**:
1. Aumentar el tamaño de `rmt_group_queue` (modificar código, línea 204)
2. Reducir la frecuencia de publicación MQTT
3. Optimizar el procesamiento

### Problema: Memoria insuficiente

**Síntomas**: Logs muestran "Failed to allocate memory for pulse group"

**Causa**: Memoria fragmentada o grupos muy grandes

**Soluciones**:
1. Reducir `RMT_RX_BUFFER_SIZE` y `mem_block_symbols`
2. Reducir el tamaño de `rmt_group_queue`
3. Verificar uso de memoria con `heap_caps_get_free_size()`

### Problema: Precisión insuficiente

**Síntomas**: Necesitas precisión mejor que 0.5μs

**Solución**: Cambiar `resolution_hz` a 80MHz en código (línea 234)

**Trade-off**: Mayor precisión (12.5ns) pero límite de 819μs por pulso

---

## Referencias

- **Código fuente principal**: `main/rmt_pulse_capture.c`
- **Header**: `main/include/rmt_pulse_capture.h`
- **Estructuras de datos**: `main/include/datastructures.h`
- **Publicador MQTT**: `main/mss_sender.c`
- **Documentación MQTT**: `docs/MQTT_TOPICS_SCHEMA.md`
- **ESP-IDF RMT Driver**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/rmt.html

---

**Última actualización**: 2024-01-XX
**Versión del código**: Basado en commit actual del repositorio
