# Configuración NVS (Non-Volatile Storage)

Este documento describe cómo configurar y usar la partición NVS para almacenar la configuración del sistema.

## Tabla de Contenidos

1. [Flasheo de Partición NVS](#flasheo-de-partición-nvs)
2. [Análisis de Claves](#análisis-de-claves)
3. [Generación de settings.bin](#generación-de-settingsbin)
4. [Verificación](#verificación)

## Flasheo de Partición NVS

La partición `nvs_settings` almacena la configuración del sistema. El offset y tamaño cambian según el tamaño de flash del ESP32.

### Offsets según tamaño de flash

#### 4MB Flash
- **Offset**: `0x3E0000`
- **Tamaño**: `0x20000` (131072 bytes = 128 KB)
- **Comando**:
  ```bash
  esptool.py write_flash 0x3E0000 settings.bin
  ```

#### 8MB Flash
- **Offset**: `0x630000`
- **Tamaño**: `0x1CC000` (1884160 bytes = ~1.99 MB)
- **Comando**:
  ```bash
  esptool.py write_flash 0x630000 settings.bin
  ```

### Uso del Makefile

El Makefile detecta automáticamente el tamaño de flash desde `sdkconfig` o `sdkconfig.defaults`:

```bash
cd partitions
make flash
```

Esto generará `settings.bin` con el tamaño correcto y lo flasheará al offset correcto.

### Flasheo manual por tamaño

Si quieres forzar un tamaño específico:

```bash
# Para 4MB
make flash-4mb

# Para 8MB
make flash-8mb
```

## Análisis de Claves

### Claves Utilizadas

Todas las claves del archivo CSV deben estar correctamente definidas. A continuación se muestra el análisis de uso de cada clave:

| Clave CSV | Carga en Código | Uso en Código | Estado |
|-----------|----------------|---------------|--------|
| `settings` | Namespace (no es clave de datos) | Namespace NVS | ✅ OK |
| `wifi_ssid` | `settings.c:203` → `wifi_essid` | `wifi.c:66` - Configuración WiFi | ✅ OK |
| `wifi_password` | `settings.c:204` → `wifi_password` | `wifi.c:67` - Contraseña WiFi | ✅ OK |
| `wifi_ntp_server` | `settings.c:205` → `wifi_ntp_server` | `sntp.c:27,35,36` - Servidor NTP | ✅ OK |
| `mqtt_host` | `settings.c:208` → `mqtt_server` | `mqtt.c:62,71,74,94` - Host MQTT | ✅ OK |
| `mqtt_port` | `settings.c:209` → `mqtt_port` | `mqtt.c:63,72,74,95` - Puerto MQTT | ✅ OK |
| `mqtt_station` | `settings.c:213` → `mqtt_station` | `mss_sender.c:29` - Estación MQTT | ✅ OK |
| `mqtt_experiment` | `settings.c:214` → `mqtt_experiment` | `mss_sender.c:30` - Experimento MQTT | ✅ OK |
| `mqtt_device_id` | `settings.c:215` → `mqtt_device_id` | `mss_sender.c:31` - ID Dispositivo MQTT | ✅ OK |

### Claves Opcionales

Algunas claves son opcionales y solo se usan en ciertas configuraciones:

| Clave CSV | Uso | Estado |
|-----------|-----|--------|
| `mqtt_user` | Autenticación MQTT (opcional) | ✅ OK |
| `mqtt_password` | Autenticación MQTT (opcional) | ✅ OK |
| `mqtt_transport` | Protocolo MQTT (mqtt/mqtts) | ✅ OK |
| `mqtt_ca_cert` | Certificado CA para MQTT TLS | ✅ OK |

### Claves NO Utilizadas

| Clave CSV | Estado | Recomendación |
|-----------|--------|---------------|
| `author` | ❌ No se usa en ningún lugar del código | **ELIMINAR** o implementar si se necesita para documentación |

### Detalles de Uso por Clave

#### `wifi_ssid`
```203:203:main/settings.c
    LOAD_AND_SET("wifi_ssid", wifi_essid);
```
- Se carga como `wifi_essid` en la estructura `nmda_init_config_t`
- Se usa en `wifi.c:66` para configurar el SSID de WiFi

#### `wifi_password`
```204:204:main/settings.c
    LOAD_AND_SET("wifi_password", wifi_password);
```
- Se carga como `wifi_password` en la estructura
- Se usa en `wifi.c:67` para configurar la contraseña de WiFi

#### `wifi_ntp_server`
```205:205:main/settings.c
    LOAD_AND_SET("wifi_ntp_server", wifi_ntp_server);
```
- Se usa en `sntp.c` para configurar el servidor NTP

#### `mqtt_host`
```208:208:main/settings.c
    LOAD_AND_SET("mqtt_host", mqtt_server);
```
- Se carga como `mqtt_server` en la estructura
- Se usa en `mqtt.c` para configurar el hostname del broker MQTT

#### `mqtt_port`
```209:209:main/settings.c
    LOAD_AND_SET("mqtt_port", mqtt_port);
```
- Se usa en `mqtt.c` para configurar el puerto del broker MQTT

#### `mqtt_station`
```213:213:main/settings.c
    LOAD_AND_SET("mqtt_station", mqtt_station);
```
- Se usa en `mss_sender.c:29` para identificar la estación en los mensajes MQTT

#### `mqtt_experiment`
```214:214:main/settings.c
    LOAD_AND_SET("mqtt_experiment", mqtt_experiment);
```
- Se usa en `mss_sender.c:30` para identificar el experimento en los mensajes MQTT

#### `mqtt_device_id`
```215:215:main/settings.c
    LOAD_AND_SET("mqtt_device_id", mqtt_device_id);
```
- Se usa en `mss_sender.c:31` para identificar el dispositivo en los mensajes MQTT

## Generación de settings.bin

1. Edita `settings.csv` (o el archivo CSV específico para tu dispositivo) con tus valores
2. Genera el binario:
   ```bash
   # Para 4MB
   ${IDF_PATH}/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py \
       generate settings.csv settings.bin 131072
   
   # Para 8MB
   ${IDF_PATH}/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py \
       generate settings.csv settings.bin 1884160
   ```

3. Verifica el contenido:
   ```bash
   python ${IDF_PATH}/components/nvs_flash/nvs_partition_tool/nvs_tool.py -i settings.bin
   ```

4. Flashea:
   ```bash
   # Para 4MB
   esptool.py write_flash 0x3E0000 settings.bin
   
   # Para 8MB
   esptool.py write_flash 0x630000 settings.bin
   ```

## Verificación

Para verificar que la partición existe y está correctamente flasheada:

```bash
# Listar particiones
python ${IDF_PATH}/components/partition_table/gen_esp32part.py \
    build/partition_table/partition-table.bin

# Deberías ver nvs_settings con el offset correcto
```

## Notas Importantes

- El tamaño del binario debe coincidir exactamente con el tamaño de la partición
- Si el binario es más grande que la partición, el flasheo fallará
- Si el binario es más pequeño, funcionará pero no usará todo el espacio disponible
- Después de flashear, el ESP32 debería poder leer la configuración desde NVS
- **Importante**: La clave correcta es `wifi_password` (no `wifi_pasword` ni `wifi_pass`)

## Archivos Relevantes

- `main/settings.c` - Carga de configuración desde NVS
- `main/include/settings.h` - Definición de estructura de configuración
- `main/wifi.c` - Uso de `wifi_ssid` y `wifi_password`
- `main/sntp.c` - Uso de `wifi_ntp_server`
- `main/mqtt.c` - Uso de `mqtt_host` y `mqtt_port`
- `main/mss_sender.c` - Uso de `mqtt_station`, `mqtt_experiment`, `mqtt_device_id`
- `partitions/settings.csv` - Archivo CSV de ejemplo
- `partitions/Makefile` - Makefile para generar y flashear settings.bin
