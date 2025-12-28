# Log de Desarrollo

## 2024-12-XX - Implementación Fases 1 y 2: Sistema RMT de captura de pulsos (base)

### Cambios realizados

**Rama creada**: `feature/rmt-pulse-capture`

**Fase 1: Configuración Kconfig y estructura base**
- **Archivo modificado**: `main/Kconfig.projbuild`
  - Añadida opción `ENABLE_GPIO_PULSE_DETECTION` (bool, default y)
  - Añadida opción `ENABLE_RMT_PULSE_DETECTION` (bool, default n)
  - Añadidas opciones de configuración RMT:
    - `RMT_COINCIDENCE_TOLERANCE_US` (int, default 10, range 1-1000)
    - `RMT_MULTIPLICITY_THRESHOLD_US` (int, default 100, range 1-10000)
    - `RMT_EVENT_BUFFER_SIZE` (int, default 100, range 10-1000)
    - `RMT_GLITCH_FILTER_NS` (int, default 1300, range 0-10000)
    - `RMT_RX_TIMEOUT_US` (int, default 1000, range 10-10000)

- **Archivo modificado**: `main/include/datastructures.h`
  - Añadidos nuevos tipos de mensaje:
    - `TM_RMT_PULSE_EVENT` (7)
    - `TM_RMT_COINCIDENCE` (8)
    - `TM_RMT_MULTIPLICITY` (9)
  - Añadidas estructuras de datos:
    - `tm_rmt_pulse_event`: evento individual de pulso (channel, duration_us, separation_us)
    - `tm_rmt_coincidence`: coincidencia detectada (type, num_channels, channels[], durations[], separations[])
    - `tm_rmt_multiplicity`: multiplicidad detectada (channel, count, max_separation_us, total_duration_us)

- **Archivo creado**: `main/include/rmt_pulse_capture.h`
  - Header para captura RMT con estructuras y funciones públicas
  - Define tipos de coincidencia (COINC_2_CH01, COINC_2_CH12, COINC_2_CH02, COINC_3)

- **Archivo creado**: `main/include/pulse_coincidence.h`
  - Header para detector de coincidencias y multiplicidades
  - Funciones de inicialización, procesamiento y estadísticas

**Fase 2: Refactorización de GPIO interrupts (hacer opcional)**
- **Archivo modificado**: `main/pulse_detection.c`
  - Todo el código envuelto con `#ifdef CONFIG_ENABLE_GPIO_PULSE_DETECTION`
  - Añadidas funciones stub cuando GPIO está desactivado (para compatibilidad con PCNT)

- **Archivo modificado**: `main/main.c`
  - `init_GPIO()` se llama siempre (necesario para PCNT), pero solo configura interrupciones si está habilitado

- **Archivo modificado**: `main/pulse_monitor.c`
  - `reconfigure_GPIO_interrupts()` solo se llama si `CONFIG_ENABLE_GPIO_PULSE_DETECTION` está activado

- **Archivo modificado**: `main/mss_sender.c`
  - Case `TM_PULSE_DETECTION` envuelto con `#ifdef CONFIG_ENABLE_GPIO_PULSE_DETECTION`
  - Variable `last_event_time` condicional
  - Variable `topic_detect` condicional

- **Archivo modificado**: `main/include/common.h`
  - Declaración de `reconfigure_GPIO_interrupts()` condicional

### Detalles técnicos

**Objetivo**: Preparar la base para implementar captura RMT mientras se mantiene la funcionalidad existente de GPIO interrupts como opcional.

**Compatibilidad**:
- El sistema PCNT sigue funcionando independientemente
- GPIO interrupts puede activarse/desactivarse sin afectar PCNT
- RMT puede activarse independientemente (implementación en fases siguientes)

**Estado actual**:
- ✅ Fase 1 completada: Configuración y estructura base lista
- ✅ Fase 2 completada: GPIO interrupts ahora es opcional
- ⏳ Fase 3 pendiente: Implementación básica de RMT - captura de eventos
- ⏳ Fase 4 pendiente: Procesamiento de eventos RMT - estadísticas básicas
- ⏳ Fase 5 pendiente: Detección de coincidencias (2 y 3 canales)
- ⏳ Fase 6 pendiente: Detección de multiplicidades
- ⏳ Fase 7 pendiente: Optimización y ajustes finales

**Próximos pasos**:
- Implementar Fase 3: Captura básica de eventos usando RMT
- Verificar compilación con diferentes combinaciones de opciones (GPIO on/off, RMT on/off)

---

## 2024-12-24 - Corrección crítica: TMP_EXT y factores de escala SPL06-001

### Cambios realizados

- **Archivo modificado**: `main/spl06.c` - Correcciones críticas en configuración y cálculos

### Problema identificado

Los valores raw de temperatura eran ~6.4 millones (ej: 6402130) cuando deberían ser ~1-3 millones.
Esto causaba lecturas de temperatura completamente erróneas.

**Dos problemas críticos encontrados:**

1. **TMP_EXT = 0 (sensor interno ASIC)**: 
   - El código configuraba `TMP_CFG = 0x24`, que usa el sensor de temperatura interno ASIC
   - **Los coeficientes de calibración están basados en el sensor MEMS externo**
   - Usar el sensor interno con calibración para el externo da valores incorrectos

2. **Factores de escala incorrectos**:
   - El código usaba `kP = 253952` y `kT = 524288`
   - Para 8x oversampling, los factores correctos según datasheet son `kP = kT = 7864320`

### Correcciones implementadas

**Configuración de registros:**
- `PRS_CFG`: Cambiado de `0x24` a `0x23` (8x oversampling correcto)
- `TMP_CFG`: Cambiado de `0x24` a `0xA3` (TMP_EXT=1 para sensor MEMS externo)

**Factores de escala:**
- Cambiado `kP` de 253952 a 7864320
- Cambiado `kT` de 524288 a 7864320

**Fórmulas verificadas** (del datasheet):
- Temperatura: `T = c0 * 0.5 + c1 * Traw_sc` donde `Traw_sc = raw / kT`
- Presión: Fórmula completa con c00, c10, c20, c30, c01, c11, c21

### Resultado esperado

Con sensor MEMS externo y factores de escala correctos para 8x oversampling,
las lecturas de temperatura y presión deberían ser correctas.

---

## 2024 - Mejoras en implementación SPL06-001

### Cambios realizados

- **Archivo modificado**: `main/spl06.c` - Corrección crítica y mejoras
- **Archivo creado**: `documents/ANALYSIS_SPL06.md` - Análisis en profundidad

### Detalles técnicos

Se han implementado mejoras críticas en el driver del sensor SPL06-001 para resolver el problema de lectura de temperatura incorrecta:

**Corrección crítica - Lectura atómica de temperatura**:
- **Problema identificado**: La función `spl06_read_raw_temperature()` leía los 3 bytes de temperatura individualmente, lo que podía causar lectura de bytes de diferentes mediciones si el sensor actualizaba los registros entre lecturas.
- **Solución implementada**: Cambio a lectura atómica de los 3 bytes en una sola transacción I2C, igual que se hace para la presión (que funcionaba correctamente).
- **Impacto**: Esta corrección debería resolver el problema de valores de temperatura incorrectos (~-2967°C).

**Mejoras adicionales**:
1. **Validación de rangos mejorada**: Añadida validación de valores raw antes de procesarlos, retornando error si están fuera de rango esperado.
2. **Reducción de logging**: Cambiado logging excesivo de `ESP_LOGI` a `ESP_LOGD` para reducir ruido en producción.
3. **Manejo de errores mejorado**: Validación de valores raw en todas las funciones de lectura (presión, temperatura, ambas).

**Compatibilidad I2C verificada**:
- ✅ El SPL06 ya estaba correctamente integrado con el sistema I2C compartido
- ✅ Usa handles persistentes creados en `i2c_bus_init()`
- ✅ Sigue el mismo patrón que el HV ADC
- ✅ Thread-safe con mutex
- ✅ Garantiza Repeated Start cuando es necesario

**Análisis completo**:
Se ha creado un documento de análisis en profundidad (`documents/ANALYSIS_SPL06.md`) que incluye:
- Análisis de la arquitectura I2C compartida
- Comparación con el patrón del HV ADC
- Identificación de problemas y causas raíz
- Propuestas de mejora implementadas
- Plan de acción para futuras mejoras

**Resultado esperado**: La lectura de temperatura debería funcionar correctamente después de esta corrección, ya que ahora lee los 3 bytes de forma atómica, igual que la presión que ya funcionaba.

## 2024 - Módulo User LED para indicación de estado del sistema

### Cambios realizados

- **Archivo creado**: `main/user_led.c` - Implementación del módulo User LED
- **Archivo creado**: `main/include/user_led.h` - Header con API pública
- **Archivo modificado**: `main/Kconfig.projbuild` - Añadidas opciones de configuración
- **Archivo modificado**: `main/CMakeLists.txt` - Añadido user_led.c a la compilación
- **Archivo modificado**: `main/pulse_detection.c` - Eliminado código del LED USER
- **Archivo modificado**: `main/main.c` - Integrada inicialización temprana del módulo

### Detalles técnicos

Se ha creado un módulo dedicado para controlar el LED USER (GPIO 32 por defecto) que permite indicar diferentes condiciones del sistema mediante patrones de destellos configurables.

**Características principales:**
- Configurable mediante `sdkconfig` (GPIO, duraciones, pausas)
- Puede deshabilitarse completamente si no hay LED conectado
- Tarea FreeRTOS dedicada para ejecución no bloqueante
- Patrones predefinidos para diferentes estados del sistema
- API simple: `user_led_init()`, `user_led_set_condition()`, `user_led_off()`

**Patrones implementados:**
- `USER_LED_BOOTING`: 2 cortos + 2 largos (sistema arrancando)
- `USER_LED_WIFI_CONNECTING`: 2 cortos (WiFi conectando)
- `USER_LED_WIFI_ERROR`: 3 cortos + 1 largo (error WiFi)
- `USER_LED_NTP_CONNECTING`: 1 corto + 1 largo (NTP conectando)
- `USER_LED_NTP_ERROR`: 1 corto + 2 largos (error NTP)
- `USER_LED_DATA_ACQUISITION`: 1 corto (sistema adquiriendo datos, parpadeo continuo)
- `USER_LED_OFF`: LED apagado

**Configuración por defecto:**
- GPIO: 32
- Duración corto: 100ms
- Duración largo: 500ms
- Pausa entre destellos: 200ms
- Pausa entre ciclos: 1000ms (1 segundo)

**Inicialización:**
El módulo se inicializa al principio de `app_main()`, justo después de `init_nvs()`, para poder indicar el estado de arranque del sistema desde el inicio.

**Arquitectura:**
- Tarea FreeRTOS con prioridad media (3)
- Cola de mensajes para recibir comandos de cambio de condición
- Patrones definidos como arrays de duraciones (valores positivos = LED ON, negativos = LED OFF)
- Los patrones se repiten indefinidamente con pausa de 1 segundo entre ciclos

**Próximos pasos:**
- Integrar llamadas a `user_led_set_condition()` en módulos WiFi y SNTP para indicar cambios de estado
- Cambiar a `USER_LED_DATA_ACQUISITION` cuando el sistema esté completamente inicializado y adquiriendo datos

## 2024 - Eliminación de función no utilizada i2c_bus_write_read

### Cambios realizados

- **Archivo modificado**: `main/i2c_bus.c`
- **Archivo modificado**: `main/include/i2c_bus.h`
- **Función eliminada**: `i2c_bus_write_read()`

### Detalles técnicos

Se eliminó la función `i2c_bus_write_read()` porque:
1. No se utiliza en ningún lugar del código
2. Todos los usos requieren Repeated Start, por lo que se usa `i2c_bus_write_read_repeated_start()` en su lugar
3. La función eliminada tenía el problema de que podía enviar STOP entre write y read, lo cual rompe dispositivos como ADS112C04 que requieren Repeated Start

**Resultado**: Código más limpio y sin funciones no utilizadas. La única función disponible para write+read es `i2c_bus_write_read_repeated_start()`, que garantiza el uso correcto de Repeated Start Condition.

## 2024 - Configuración de navegación de código en Cursor con clangd

### Cambios realizados

- **Archivo creado**: `.vscode/c_cpp_properties.json` (para extensión C/C++ de Microsoft)
- **Archivo creado**: `.clangd` (configuración para clangd)
- **Enlace simbólico creado**: `compile_commands.json` -> `build/compile_commands.json`
- **Objetivo**: Hacer navegable el código de funciones de ESP-IDF como `i2c_master_transmit_receive` en Cursor

### Detalles técnicos

Se ha configurado IntelliSense para que Cursor pueda navegar a las definiciones de funciones de ESP-IDF:

1. **Archivo de configuración**: `.vscode/c_cpp_properties.json`
   - Configurado para usar `compile_commands.json` del directorio `build/`
   - Incluye paths de ESP-IDF y componentes del proyecto
   - Define macros y estándares de compilación necesarios

2. **Enlace simbólico**: Se creó un enlace desde la raíz del proyecto al `compile_commands.json` en `build/` para facilitar que Cursor lo encuentre automáticamente.

**Resultado**: Configurado clangd para navegación de código. clangd es más confiable que la extensión C/C++ de Microsoft para encontrar implementaciones de funciones.

**Configuración clangd**:
1. Archivo `.clangd` creado en la raíz del proyecto
2. Configurado para usar `compile_commands.json` del directorio `build/`
3. Incluye paths de ESP-IDF necesarios

**Uso**:
- Instalar extensión "clangd" en Cursor (llvm-vs-code-extensions.vscode-clangd)
- Desactivar extensión "C/C++" de Microsoft para evitar conflictos
- Recargar ventana de Cursor
- clangd indexará automáticamente usando `compile_commands.json`
- Usar `Cmd+Click` o `F12` para ir a definición
- Usar `Shift+F12` o `Cmd+Shift+F12` para ir a implementación

**Nota**: clangd indexa en segundo plano. Puede tardar unos minutos la primera vez. Verificar que el `compile_commands.json` esté actualizado ejecutando `idf.py build` si es necesario.

## 2024 - Eliminación de mensajes de error en escaneo I2C

### Cambios realizados

- **Archivo modificado**: `main/i2c_bus.c`
- **Función**: `i2c_bus_scan()`
- **Cambio**: Eliminados los mensajes de advertencia (`ESP_LOGW`) que se mostraban cuando no se detectaban dispositivos I2C en el bus durante la secuencia de arranque.
- **Resultado**: El log queda más compacto y solo muestra información cuando se encuentran dispositivos. Si no hay dispositivos, la función completa el escaneo silenciosamente.

### Detalles técnicos

Se eliminaron las siguientes líneas de la función `i2c_bus_scan()`:
- `ESP_LOGW(TAG, "No I2C devices found on bus");`
- `ESP_LOGW(TAG, "Check connections: SDA=GPIO%d, SCL=GPIO%d", ...);`

Ahora solo se muestra un mensaje informativo cuando se encuentran dispositivos:
- `ESP_LOGI(TAG, "Found %d device(s) on I2C bus", found_count);`

**Nota**: Los mensajes de error del driver I2C (`i2c.master`) siguen apareciendo durante el escaneo, ya que es el comportamiento normal del driver al intentar comunicarse con direcciones que no tienen dispositivos conectados.

## 2024-12-XX - Documentación completa del sistema PBURST

### Cambios realizados

- **Archivo creado**: `docs/PBURST_DOCUMENTATION.md` - Documentación técnica completa del sistema de captura de pulsos RMT

### Detalles técnicos

Se ha creado una documentación exhaustiva que cubre todos los aspectos del sistema PBURST:

**Contenido de la documentación**:
1. **Introducción**: Propósito, características principales y casos de uso
2. **Arquitectura del Sistema**: Diagrama de componentes, estructuras de datos y flujo de información
3. **Flujo de Datos**: Proceso completo desde captura hardware hasta publicación MQTT
4. **Interpretación de Campos**: Explicación detallada de cada campo del mensaje JSON:
   - `start_datetime`: Timestamp Unix con precisión de microsegundos
   - `duration_us`: Duración del pulso en nivel HIGH
   - `separation_us`: Periodo entre pulsos (start-to-start)
   - `channel`: Identificador del canal físico
   - `symbols`: Número de pulsos en el grupo
5. **Precisiones y Limitaciones**: 
   - Resolución temporal (500ns con 2MHz)
   - Limitaciones de hardware (duración mínima/máxima, tamaño de grupo)
   - Limitaciones de software (colas, memoria)
6. **Configuraciones Disponibles**: 
   - Configuraciones mediante `menuconfig` (glitch filter, timeout, etc.)
   - Configuraciones hardcodeadas (resolución RMT, tamaño de buffer, etc.)
   - Instrucciones para modificar cada configuración
7. **Ejemplos con Diferentes Configuraciones**: 
   - Pulsos rápidos (6μs, periodo 60μs)
   - Pulsos largos (100μs, periodo 1ms)
   - Pulsos muy cortos (2μs) con filtro ajustado
   - Pulsos muy largos (5ms) con timeout aumentado
   - Grupos grandes (20 pulsos)
8. **Cómo Cambiar las Configuraciones**: 
   - Instrucciones paso a paso para `menuconfig`
   - Instrucciones para modificar código fuente
9. **Troubleshooting**: 
   - Problemas comunes y soluciones
   - Diagnóstico de errores

**Correcciones documentadas**:
- Conversión de timestamps de boot time a Unix timestamp con precisión de microsegundos
- Cálculo correcto de `separation_us` como periodo (start-to-start) en lugar de gap (end-to-start)
- Explicación de la precisión de 0.5μs (resolución de 500ns redondeada)

**Resultado**: Documentación completa que permite a usuarios y desarrolladores entender completamente el funcionamiento del sistema, interpretar correctamente los datos, y configurar el sistema según sus necesidades específicas.
