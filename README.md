# Neutron Monitor Data Acquisition (NMDA)

Sistema de adquisición de datos para monitores de neutrones basado en ESP32. Este proyecto implementa un sistema embebido que monitorea pulsos de detección en múltiples canales, cuenta eventos periódicamente y transmite los datos de telemetría mediante MQTT.

## Características

- **Monitoreo de pulsos en 3 canales**: Detección y conteo de pulsos en tiempo real mediante GPIO (pines 25, 26, 27)
- **Conteo periódico**: Integración de pulsos cada 10 segundos con timestamp preciso
- **Detección de eventos**: Captura inmediata de eventos de pulsos con timestamp de microsegundos
- **Comunicación MQTT**: Transmisión de datos de telemetría a un broker MQTT configurable
- **Sincronización NTP**: Sincronización de tiempo mediante servidor NTP
- **Configuración flexible**: Sistema de configuración mediante partición NVS personalizada
- **Soporte OTA**: Preparado para actualizaciones Over-The-Air (actualmente deshabilitado)

## Hardware Requerido

- ESP32 con 4MB o 8MB de flash (ver [README_FLASH_SIZE.md](README_FLASH_SIZE.md) para más detalles)
- Sensores o dispositivos de detección conectados a los pines GPIO:
  - Canal 1: GPIO 25
  - Canal 2: GPIO 26
  - Canal 3: GPIO 27

## Requisitos de Software

- ESP-IDF >= 4.1.0 (probado con ESP-IDF 5.4.0)
- CMake >= 3.5
- Python 3.x (para herramientas de particiones NVS)
- Componente `leeebo/esp-inih` (gestionado automáticamente por IDF Component Manager)

## Compilación

1. Configurar el entorno de ESP-IDF:
```bash
. $IDF_PATH/export.sh
```

2. Configurar el proyecto:
```bash
idf.py menuconfig
```

Asegúrate de configurar las siguientes opciones en `sdkconfig`:
```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions/partitions.csv"
CONFIG_PARTITION_TABLE_OFFSET=0x8000
```

3. Compilar el proyecto:
```bash
idf.py build
```

4. Flashear el firmware:
```bash
idf.py flash
```

## Configuración

### Particiones

El ESP32 tiene la posibilidad de particionar su memoria interna no volátil de 8MB. Cada una de estas particiones se usa para un fin específico. Cada partición se identifica con una etiqueta. En concreto en nuestro sistema `esp-nmda`, la tabla de particiones está definida en `partitions/partitions.csv` y tiene el siguiente contenido:

```
# Name,       Type, SubType, Offset,   Size,  Flags
nvs,          data, nvs,     0x9000,   0x24000
otadata,      data, ota,     0x2D000,  0x2000
phy_init,     data, phy,     0x2F000,  0x1000
factory,      app,  factory, 0x30000,  0x200000
ota_0,        app,  ota_0,   0x230000, 0x200000
ota_1,        app,  ota_1,   0x430000, 0x200000
nvs_settings, data, nvs,     0x630000, 0x1CC000
```

Las posiciones concretas y el tamaño de cada partición se hicieron con la ayuda de [esta hoja de cálculo](https://docs.google.com/spreadsheets/d/1GGQgFF905QJ1zDRdo4AWnYGhVPz9GH1OXI0z4Kxn5Zk/edit#gid=0).

La tabla de particiones define cómo se divide la memoria flash en un dispositivo ESP32 y especifica el propósito de cada partición. Aquí tienes una explicación del propósito de cada partición:

1. **nvs** (Type: data, SubType: nvs):
   - **Offset**: 0x9000
   - **Size**: 0x24000 (144 KB)
   - **Propósito**: Almacena datos no volátiles (NVS) utilizados para configuración y almacenamiento de datos en la memoria flash. Los datos en esta partición son persistentes y se utilizan para configurar diversos aspectos del dispositivo, como la configuración de Wi-Fi, la configuración de la aplicación, etc.

2. **otadata** (Type: data, SubType: ota):
   - **Offset**: 0x2D000
   - **Size**: 0x2000 (8 KB)
   - **Propósito**: Almacena datos relacionados con las actualizaciones "Over-The-Air" (OTA) del firmware. Puede contener información sobre la versión actual del firmware y otros datos relacionados con las actualizaciones OTA.

3. **phy_init** (Type: data, SubType: phy):
   - **Offset**: 0x2F000
   - **Size**: 0x1000 (4 KB)
   - **Propósito**: Almacena datos de inicialización específicos del hardware relacionados con el funcionamiento de la capa física (PHY) de la comunicación inalámbrica. Es esencial para la configuración y el funcionamiento correcto de las comunicaciones inalámbricas.

4. **factory** (Type: app, SubType: factory):
   - **Offset**: 0x30000
   - **Size**: 0x200000 (2 MB)
   - **Propósito**: Almacena la imagen del firmware de fábrica. Es el firmware original del dispositivo antes de cualquier actualización OTA. Siempre se puede volver a esta imagen si es necesario.

5. **ota_0** (Type: app, SubType: ota_0):
   - **Offset**: 0x230000
   - **Size**: 0x200000 (2 MB)
   - **Propósito**: Almacena una de las imágenes del firmware para las actualizaciones OTA. En el caso de actualizaciones OTA, el firmware nuevo se carga en una de estas particiones mientras que la otra partición (ota_1 en este caso) contiene el firmware en uso actualmente. Esto permite cambiar a la nueva imagen después de una actualización exitosa.

6. **ota_1** (Type: app, SubType: ota_1):
   - **Offset**: 0x430000
   - **Size**: 0x200000 (2 MB)
   - **Propósito**: Al igual que ota_0, esta partición almacena una de las imágenes del firmware para las actualizaciones OTA. Durante una actualización, se utiliza una partición mientras que la otra contiene el firmware actual en uso.

7. **nvs_settings** (Type: data, SubType: nvs):
   - **Offset**: 0x630000
   - **Size**: 0x1CC000 (1.99 MB)
   - **Propósito**: Esta partición adicional almacena datos NVS para configuración y almacenamiento de datos adicionales. En concreto, los valores de esta partición permiten configurar cada uno de los sistemas de adquisición de forma particular.

### Configuración de parámetros específicos

Los datos específicos de configuración se generan a partir de un archivo que se encuentra en `partitions/settings.csv`. Existe en ese mismo directorio un ejemplo `settings.sample.csv`.

El archivo CSV debe contener las siguientes claves de configuración:

- `wifi_ssid`: SSID de la red Wi-Fi
- `wifi_pasword`: Contraseña de la red Wi-Fi
- `mqtt_host`: Dirección IP o hostname del broker MQTT
- `mqtt_port`: Puerto del broker MQTT (típicamente 1883)
- `mqtt_station`: Identificador de la estación
- `mqtt_experiment`: Identificador del experimento
- `mqtt_device_id`: Identificador único del dispositivo

Para generar el archivo binario correspondiente:
```bash
cd partitions
make
```

Para flashear la partición NVS del dispositivo con el contenido de este archivo:
```bash
cd partitions
make flash
```

**Nota**: Asegúrate de tener configurado `IDF_PATH` en tu entorno antes de ejecutar estos comandos.

## Estructura del Proyecto

```
neutron-monitor-hv-psu/
├── main/                    # Código fuente principal
│   ├── include/            # Headers
│   │   ├── common.h        # Definiciones comunes
│   │   ├── datastructures.h # Estructuras de datos de telemetría
│   │   ├── mqtt.h          # Funciones MQTT
│   │   ├── pulse_monitor.h # Monitoreo de pulsos
│   │   ├── settings.h      # Gestión de configuración
│   │   ├── sntp.h          # Sincronización NTP
│   │   └── wifi.h          # Configuración Wi-Fi
│   ├── main.c              # Punto de entrada de la aplicación
│   ├── mqtt.c              # Implementación MQTT
│   ├── mss_sender.c        # Envío de mensajes de telemetría
│   ├── pulse_monitor.c     # Conteo periódico de pulsos
│   ├── pulse_detection.c   # Detección de eventos de pulsos
│   ├── settings.c          # Carga de configuración desde NVS
│   ├── sntp.c              # Sincronización de tiempo
│   └── wifi.c              # Gestión de conexión Wi-Fi
├── partitions/             # Configuración de particiones
│   ├── partitions.csv      # Tabla de particiones
│   ├── settings.sample.csv # Ejemplo de configuración
│   └── Makefile            # Generación de partición NVS
├── CMakeLists.txt          # Configuración CMake principal
├── sdkconfig.defaults      # Configuración por defecto de ESP-IDF
└── README.md              # Este archivo
```

## Tipos de Mensajes de Telemetría

El sistema genera y transmite los siguientes tipos de mensajes MQTT:

1. **TM_PULSE_COUNT** (`{station}/{experiment}/{device}/pcnt`):
   - Conteo de pulsos integrado durante un período (10 segundos)
   - Contiene: timestamp, conteos por canal (ch01, ch02, ch03), intervalo de integración

2. **TM_PULSE_DETECTION** (`{station}/{experiment}/{device}/detect`):
   - Detección inmediata de eventos de pulsos
   - Contiene: timestamp de microsegundos, estado de cada canal al momento del evento

3. **TM_TIME_SYNCHRONIZER** (`{station}/{experiment}/{device}/timesync`):
   - Mensajes de sincronización de tiempo
   - Contiene: timestamp, contador de CPU

4. **Status** (`{station}/{experiment}/{device}/status`):
   - Mensajes de estado del sistema

## Funcionamiento

1. **Inicialización**: El sistema carga la configuración desde la partición NVS `nvs_settings`
2. **Conexión**: Se conecta a la red Wi-Fi configurada y sincroniza el tiempo con un servidor NTP
3. **Monitoreo**: Inicia dos tareas principales:
   - **task_pcnt**: Cuenta pulsos periódicamente (cada 10 segundos) en los 3 canales
   - **task_detection**: Detecta eventos de pulsos en tiempo real mediante interrupciones GPIO
4. **Transmisión**: Los datos de telemetría se envían mediante MQTT a los topics configurados
5. **Sincronización**: El sistema mantiene sincronización de tiempo para timestamps precisos

## Arquitectura

El sistema utiliza FreeRTOS para gestionar múltiples tareas:

- **Core 0**: Tareas de comunicación (Wi-Fi, MQTT, envío de mensajes)
- **Core 1**: Tareas de adquisición de datos (conteo de pulsos)

Las tareas se comunican mediante colas de mensajes (`telemetry_queue`) y semáforos para sincronización.
