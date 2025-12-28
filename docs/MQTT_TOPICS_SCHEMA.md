# Esquema de Topics MQTT

Este documento describe el esquema de topics MQTT utilizado en el sistema, su relación con la configuración en `settings.csv`, y las propuestas para nuevos topics.

## Tabla de Contenidos

1. [Esquema Actual de Topics](#esquema-actual-de-topics)
2. [Relación con settings.csv](#relación-con-settingscsv)
3. [Compatibilidad con Telegraf](#compatibilidad-con-telegraf)
4. [Topics Actuales](#topics-actuales)
5. [Propuesta de Topics para Meteo](#propuesta-de-topics-para-meteo)
6. [Recomendaciones](#recomendaciones)

## Esquema Actual de Topics

El sistema construye los topics MQTT usando el siguiente formato:

```
{station}/{experiment}/{device}/{service}
```

Donde:
- **station**: Identificador de la estación (ej: `orca`, `op64`)
- **experiment**: Identificador del experimento (ej: `nemo`, `cosmic_rays`)
- **device**: Identificador único del dispositivo (ej: `op64hive`, MAC address)
- **service**: Tipo de servicio/datos (ej: `pcnt`, `spl06`, `detect`, `rmt_pulse`, `timesync`, `status`)

### Ejemplo de Topic Completo

Con la configuración:
- `mqtt_station = "orca"`
- `mqtt_experiment = "nemo"`
- `mqtt_device_id = "b8d61aa73b90"`

El topic para pulsos sería:
```
orca/nemo/b8d61aa73b90/pcnt
```

## Relación con settings.csv

Los topics se construyen a partir de las siguientes claves en `settings.csv`:

| Clave CSV | Variable en Código | Uso en Topic | Ejemplo |
|-----------|-------------------|--------------|---------|
| `mqtt_station` | `station` | Primera parte del topic | `orca` |
| `mqtt_experiment` | `experiment` | Segunda parte del topic | `nemo` |
| `mqtt_device_id` | `device` | Tercera parte del topic | `b8d61aa73b90` |

### Implementación en Código

```40:52:main/mss_sender.c
    sprintf(topic_base, "%s/%s/%s", station, experiment, device);
    sprintf(topic_status, "%s/status", topic_base);
    sprintf(topic_pcnt, "%s/pcnt", topic_base);
#ifdef CONFIG_ENABLE_GPIO_PULSE_DETECTION
    sprintf(topic_detect, "%s/detect", topic_base);
#endif
#ifdef CONFIG_ENABLE_RMT_PULSE_DETECTION
    sprintf(topic_rmt_pulse, "%s/rmt_pulse", topic_base);
#endif
    sprintf(topic_timesync, "%s/timesync", topic_base);
#ifdef CONFIG_ENABLE_SPL06
    sprintf(topic_spl06, "%s/spl06", topic_base);
#endif
```

### Valores por Defecto

Si alguna de las claves no está definida en `settings.csv`, el código usa valores por defecto:

```34:36:main/mss_sender.c
    if (!station) station = "default";
    if (!experiment) experiment = "default";
    if (!device) device = "default";
```

**Recomendación**: Siempre definir estas tres claves en `settings.csv` para evitar topics con valores por defecto.

## Compatibilidad con Telegraf

La configuración de Telegraf espera el siguiente esquema de topics:

### Configuración Telegraf

```toml
[[inputs.mqtt_consumer]]
  topics = [
    "orca/nemo/+/pcnt"
  ]
  
  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "orca/+/+/+"
    tags = "station/experiment/esp32id/service"
```

### Análisis del Esquema Telegraf

Telegraf espera:
- **Formato**: `{station}/{experiment}/{esp32id}/{service}`
- **Wildcard**: `+` para capturar cualquier valor en esa posición
- **Tags extraídos**: `station`, `experiment`, `esp32id`, `service`

### Compatibilidad Actual

El esquema actual del código es **compatible** con Telegraf:

| Componente | Código | Telegraf | Compatible |
|------------|--------|----------|------------|
| station | `mqtt_station` | `station` | ✅ Sí |
| experiment | `mqtt_experiment` | `experiment` | ✅ Sí |
| device/esp32id | `mqtt_device_id` | `esp32id` | ✅ Sí (mismo concepto) |
| service | Hardcoded | `service` | ✅ Sí |

**Nota**: `mqtt_device_id` en el código corresponde a `esp32id` en Telegraf. Ambos identifican de forma única el dispositivo.

## Topics Actuales

### Lista de Services Implementados

| Service | Topic Completo | Descripción | Estado |
|---------|---------------|-------------|--------|
| `status` | `{station}/{experiment}/{device}/status` | Estado del sistema | ✅ Implementado |
| `pcnt` | `{station}/{experiment}/{device}/pcnt` | Contadores de pulsos (PCNT) | ✅ Implementado |
| `detect` | `{station}/{experiment}/{device}/detect` | Detección de pulsos (GPIO) | ✅ Implementado (opcional) |
| `pburst` | `{station}/{experiment}/{device}/pburst` | Eventos RMT de pulsos (bursts) | ✅ Implementado (opcional) |
| `timesync` | `{station}/{experiment}/{device}/timesync` | Sincronización de tiempo | ✅ Implementado |
| `meteo` | `{station}/{experiment}/{device}/meteo` | Datos meteorológicos | ✅ Implementado (opcional) |

### Ejemplos de Topics Reales

Con configuración:
- `mqtt_station = "orca"`
- `mqtt_experiment = "nemo"`
- `mqtt_device_id = "b8d61aa73b90"`

Los topics generados serían:
```
orca/nemo/b8d61aa73b90/status
orca/nemo/b8d61aa73b90/pcnt
orca/nemo/b8d61aa73b90/detect
orca/nemo/b8d61aa73b90/pburst
orca/nemo/b8d61aa73b90/timesync
orca/nemo/b8d61aa73b90/meteo
```

## Descripción Detallada de Topics

### `status` - Estado del Sistema

**Topic**: `{station}/{experiment}/{device}/status`

**Propósito**: Publica mensajes de estado del sistema para indicar que el dispositivo está funcionando y el módulo `mss_sender` está activo.

**Frecuencia**: Se publica una vez al inicio cuando el sistema se conecta a MQTT.

**Formato JSON**:
```json
"mss_sender is running"
```

**Campos**:
- Mensaje simple de texto indicando que el sistema está operativo.

**Ejemplo**:
```
orca/nemo/b8d61aa73b90/status → "mss_sender is running"
```

---

### `pcnt` - Contadores de Pulsos (PCNT)

**Topic**: `{station}/{experiment}/{device}/pcnt`

**Propósito**: Publica los contadores acumulados de pulsos de los tres canales (ch01, ch02, ch03) medidos por el periférico PCNT del ESP32. Estos son contadores de pulsos integrados durante un intervalo de tiempo específico.

**Frecuencia**: Publicado periódicamente según la configuración del sistema de monitoreo de pulsos (típicamente cada 60 segundos).

**Formato JSON**:
```json
{
  "start_datetime": "1234567890123456",
  "datetime": "1234567890123456",
  "ch01": "12345",
  "ch02": "67890",
  "ch03": "11111",
  "Interval_s": "60"
}
```

**Campos**:
- `start_datetime` (string): Timestamp de inicio del intervalo de integración en microsegundos (Unix timestamp).
- `datetime` (string): Timestamp de fin del intervalo de integración en microsegundos (Unix timestamp).
- `ch01` (string): Contador acumulado del canal 1 durante el intervalo.
- `ch02` (string): Contador acumulado del canal 2 durante el intervalo.
- `ch03` (string): Contador acumulado del canal 3 durante el intervalo.
- `Interval_s` (string): Duración del intervalo de integración en segundos.

**Ejemplo**:
```
orca/nemo/b8d61aa73b90/pcnt → {"start_datetime":"1703764800000000","datetime":"1703764860000000","ch01":"12345","ch02":"67890","ch03":"11111","Interval_s":"60"}
```

**Uso en Telegraf**: Este topic es el principal para análisis de datos de rayos cósmicos, permitiendo calcular tasas de conteo y detectar variaciones temporales.

---

### `detect` - Detección de Pulsos (GPIO)

**Topic**: `{station}/{experiment}/{device}/detect`

**Propósito**: Publica eventos individuales de detección de pulsos capturados mediante interrupciones GPIO. Cada mensaje representa un evento de detección simultáneo en los tres canales.

**Frecuencia**: Publicado cada vez que se detecta un pulso en cualquiera de los canales (eventos en tiempo real).

**Condición**: Solo disponible si `CONFIG_ENABLE_GPIO_PULSE_DETECTION` está habilitado.

**Formato JSON**:
```json
{
  "datetime": "1234567890123456",
  "ch01": "1",
  "ch02": "0",
  "ch03": "1"
}
```

**Campos**:
- `datetime` (string): Timestamp del evento en microsegundos (Unix timestamp).
- `ch01` (string): "1" si se detectó pulso en canal 1, "0" si no.
- `ch02` (string): "1" si se detectó pulso en canal 2, "0" si no.
- `ch03` (string): "1" si se detectó pulso en canal 3, "0" si no.

**Ejemplo**:
```
orca/nemo/b8d61aa73b90/detect → {"datetime":"1703764800123456","ch01":"1","ch02":"0","ch03":"1"}
```

**Uso**: Útil para análisis de coincidencias en tiempo real y detección de eventos simultáneos entre canales.

---

### `pburst` - Eventos RMT de Pulsos (Bursts)

**Topic**: `{station}/{experiment}/{device}/pburst`

**Propósito**: Publica grupos de pulsos (bursts) capturados mediante el periférico RMT del ESP32. Cada mensaje contiene un grupo completo de pulsos detectados en un canal, incluyendo información detallada de duración y separación entre pulsos.

**Frecuencia**: Publicado cada vez que se completa la captura de un grupo de pulsos en un canal.

**Condición**: Solo disponible si `CONFIG_ENABLE_RMT_PULSE_DETECTION` está habilitado.

**Formato JSON**:
```json
{
  "start_datetime": "1234567890123456",
  "channel": "ch1",
  "symbols": 3,
  "pulses": [
    {
      "duration_us": 1250,
      "separation_us": -1
    },
    {
      "duration_us": 2300,
      "separation_us": 500
    },
    {
      "duration_us": 1800,
      "separation_us": 300
    }
  ]
}
```

**Campos**:
- `start_datetime` (string): Timestamp de inicio del primer pulso del grupo en microsegundos (Unix timestamp).
- `channel` (string): Canal donde se detectó el grupo ("ch1", "ch2", o "ch3").
- `symbols` (number): Número de pulsos en el grupo.
- `pulses` (array): Array de objetos, cada uno representando un pulso:
  - `duration_us` (number): Duración del pulso en microsegundos.
  - `separation_us` (number): Separación con el pulso anterior en microsegundos. `-1` indica que es el primer pulso del grupo.

**Ejemplo**:
```
orca/nemo/b8d61aa73b90/pburst → {"start_datetime":"1703764800123456","channel":"ch1","symbols":3,"pulses":[{"duration_us":1250,"separation_us":-1},{"duration_us":2300,"separation_us":500},{"duration_us":1800,"separation_us":300}]}
```

**Uso**: Permite análisis detallado de la forma de onda de los pulsos, detección de multiplicidades (múltiples pulsos en un mismo canal), y análisis de patrones temporales en los eventos de rayos cósmicos.

---

### `timesync` - Sincronización de Tiempo

**Topic**: `{station}/{experiment}/{device}/timesync`

**Propósito**: Publica eventos de sincronización de tiempo del sistema, incluyendo información sobre el contador de CPU para sincronización precisa entre dispositivos.

**Frecuencia**: Publicado cuando se produce un evento de sincronización de tiempo (típicamente después de sincronización NTP).

**Formato JSON**:
```json
{
  "datetime": "1234567890123456",
  "cpu_lnd": "1234567890"
}
```

**Campos**:
- `datetime` (string): Timestamp del evento de sincronización en microsegundos (Unix timestamp).
- `cpu_lnd` (string): Contador de CPU en el momento de la sincronización, usado para referencia temporal precisa.

**Ejemplo**:
```
orca/nemo/b8d61aa73b90/timesync → {"datetime":"1703764800000000","cpu_lnd":"1234567890"}
```

**Uso**: Permite sincronizar timestamps entre múltiples dispositivos y corregir deriva temporal del reloj del sistema.

---

### `meteo` - Datos Meteorológicos

**Topic**: `{station}/{experiment}/{device}/meteo`

**Propósito**: Publica datos meteorológicos del sensor ambiental (actualmente SPL06-001, pero genérico para cualquier sensor meteorológico futuro). Incluye presión atmosférica, temperatura, y valores calculados como QNH (presión al nivel del mar).

**Frecuencia**: Publicado periódicamente según la configuración `CONFIG_SPL06_PUBLISH_PERIOD_SEC` (típicamente cada 60 segundos).

**Condición**: Solo disponible si `CONFIG_ENABLE_SPL06` está habilitado.

**Formato JSON**:
```json
{
  "datetime": "1234567890123456",
  "pressure_pa": "101325.00",
  "pressure_hpa": "1013.25",
  "temperature_celsius": "20.50",
  "qnh_hpa": "1013.25"
}
```

**Campos**:
- `datetime` (string): Timestamp de la medición en microsegundos (Unix timestamp).
- `pressure_pa` (string): Presión atmosférica en Pascales (Pa), con 2 decimales.
- `pressure_hpa` (string): Presión atmosférica en hectopascales (hPa), con 2 decimales.
- `temperature_celsius` (string): Temperatura en grados Celsius (°C), con 2 decimales.
- `qnh_hpa` (string): Presión reducida al nivel del mar (QNH) en hectopascales (hPa), calculada usando la altitud de la estación configurada en `CONFIG_SPL06_STATION_ALTITUDE_M`, con 2 decimales.

**Ejemplo**:
```
orca/nemo/b8d61aa73b90/meteo → {"datetime":"1703764800000000","pressure_pa":"101325.00","pressure_hpa":"1013.25","temperature_celsius":"20.50","qnh_hpa":"1013.25"}
```

**Uso**: 
- Corrección de datos de rayos cósmicos por presión atmosférica (los muones dependen de la presión).
- Monitoreo ambiental de la estación.
- Análisis de correlación entre condiciones meteorológicas y tasas de detección.
- El valor QNH permite comparar presiones entre estaciones a diferentes altitudes.

**Nota**: El nombre `meteo` es genérico y permite que en el futuro se añadan más sensores meteorológicos (humedad, viento, etc.) sin cambiar el nombre del topic.

## Cambios Implementados

### Cambio de `spl06` a `meteo`

**Implementado**: El topic `spl06` ha sido cambiado a `meteo` para hacerlo genérico y compatible con cualquier sensor meteorológico futuro.

**Ventajas**:
1. **Genérico**: No está atado a un sensor específico
2. **Extensible**: Funciona con cualquier sensor meteorológico (SPL06, BME280, BMP280, etc.)
3. **Consistente**: Sigue el patrón de nombres descriptivos (`pcnt`, `detect`, etc.)
4. **Compatible con Telegraf**: Se puede configurar fácilmente en Telegraf

### Cambio de `rmt_pulse` a `pburst`

**Implementado**: El topic `rmt_pulse` ha sido cambiado a `pburst` (pulse burst) para reflejar mejor que contiene grupos/bursts de pulsos en lugar de pulsos individuales.

**Ventajas**:
1. **Más descriptivo**: Indica claramente que contiene grupos de pulsos
2. **Más corto**: Nombre más conciso
3. **Consistente**: Sigue el patrón de nombres cortos y descriptivos

### Configuración Telegraf Actualizada

```toml
[[inputs.mqtt_consumer]]
  topics = [
    "orca/nemo/+/pcnt",
    "orca/nemo/+/meteo",  # Topic genérico para datos meteorológicos
    "orca/nemo/+/pburst"  # Topic para bursts de pulsos RMT
  ]
  
  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "orca/+/+/+"
    tags = "station/experiment/esp32id/service"
  
  [[inputs.mqtt_consumer.json_v2]]
    # Campos para pcnt (ya existentes)
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "ch01"
      type = "int"
      optional = true
    # ... otros campos de pcnt ...
    
    # Campos para meteo
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "pressure_pa"
      type = "float"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "pressure_hpa"
      type = "float"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "temperature_celsius"
      type = "float"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "qnh_hpa"
      type = "float"
      optional = true
    
    # Campos para pburst
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "channel"
      type = "string"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "symbols"
      type = "int"
      optional = true
    # Los pulsos se procesan como array anidado
```

## Recomendaciones

### 1. Convenciones de Nombres

- **Services**: Usar nombres genéricos y descriptivos (`pcnt`, `meteo`, `detect`, `pburst`)
- **Evitar**: Nombres específicos de hardware (`spl06`, `bme280`) a menos que sea necesario
- **Consistencia**: Mantener nombres cortos y en minúsculas
- **Descriptivos**: Los nombres deben indicar claramente el tipo de datos que contienen

### 2. Documentación de Topics

Mantener este documento actualizado cuando se añadan nuevos services/topics. Cada nuevo topic debe incluir:
- Propósito del topic
- Frecuencia de publicación
- Formato JSON completo con descripción de campos
- Ejemplos de uso

### 3. Validación de Configuración

Añadir validación en el código para asegurar que `mqtt_station`, `mqtt_experiment` y `mqtt_device_id` están definidos antes de construir topics.

### 4. Compatibilidad con Telegraf

Al añadir nuevos topics, actualizar la configuración de Telegraf para incluir:
- Suscripción al nuevo topic con wildcards apropiados
- Parsing de campos JSON según el formato del mensaje
- Configuración de procesadores/agregadores si es necesario

## Referencias

- **Código fuente**: `main/mss_sender.c` - Construcción de topics
- **Configuración**: `partitions/settings.csv` - Valores de `mqtt_station`, `mqtt_experiment`, `mqtt_device_id`
- **Telegraf**: Configuración de `inputs.mqtt_consumer` para parsing de topics

## Ejemplo Completo de Configuración

### settings.csv

```csv
key,type,encoding,value
settings,namespace,,
mqtt_station,data,string,orca
mqtt_experiment,data,string,nemo
mqtt_device_id,data,string,b8d61aa73b90
```

### Topics Generados

```
orca/nemo/b8d61aa73b90/status
orca/nemo/b8d61aa73b90/pcnt
orca/nemo/b8d61aa73b90/detect
orca/nemo/b8d61aa73b90/pburst
orca/nemo/b8d61aa73b90/timesync
orca/nemo/b8d61aa73b90/meteo
```

### Configuración Telegraf

```toml
[[inputs.mqtt_consumer]]
  servers = ["tcp://localhost:1883"]
  topics = [
    "orca/nemo/+/pcnt",
    "orca/nemo/+/detect",
    "orca/nemo/+/pburst",
    "orca/nemo/+/timesync",
    "orca/nemo/+/meteo"
  ]
  
  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "orca/+/+/+"
    tags = "station/experiment/esp32id/service"
```
