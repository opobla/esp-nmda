# Configuración de Tamaño de Flash

## Contexto

Este proyecto soporta ESP32 con **4MB** o **8MB** de flash. La configuración se puede cambiar fácilmente usando el script `switch_flash_size.sh`.

**Importante**: Este proyecto utiliza diferentes modelos de ESP32 que tienen diferentes capacidades de memoria flash:
- **Algunos ESP32 tienen 4MB de flash**
- **Otros ESP32 tienen 8MB de flash**

**No es un problema de detección**, sino que simplemente hay diferentes modelos de hardware. Es importante compilar el firmware con la configuración correcta según el modelo de ESP32 que se vaya a utilizar.

## Introducción a la Memoria Flash del ESP32

### ¿Qué es la Memoria Flash?

La memoria flash es un tipo de memoria no volátil (persistente) que se utiliza en los ESP32 para almacenar:
- **Firmware de la aplicación**: El código del programa que ejecuta el ESP32
- **Bootloader**: Código que se ejecuta al arrancar y carga la aplicación
- **Datos de configuración**: Parámetros del sistema que deben persistir entre reinicios
- **Datos de usuario**: Información que la aplicación necesita guardar permanentemente

A diferencia de la RAM, la memoria flash **no se borra** cuando se desconecta la alimentación, lo que la hace ideal para almacenar firmware y configuraciones.

### Características de la Flash en ESP32

- **Tamaños comunes**: 4MB, 8MB, 16MB (dependiendo del modelo)
- **Persistencia**: Los datos se mantienen sin alimentación
- **Durabilidad limitada**: Tiene un número finito de ciclos de escritura (típicamente 10,000-100,000)
- **Organización en sectores**: Se organiza en bloques que deben borrarse antes de escribir
- **Acceso**: Se accede mediante la API de ESP-IDF, no directamente

### Principales Usos de la Flash en ESP32

1. **Almacenamiento de Firmware**
   - El código compilado de la aplicación se almacena en particiones de tipo `app`
   - Permite actualizaciones sin necesidad de reprogramar físicamente el dispositivo

2. **Configuración del Sistema**
   - Parámetros de Wi-Fi, MQTT, y otros servicios
   - Configuraciones que deben persistir entre reinicios
   - Se almacenan en particiones NVS (Non-Volatile Storage)

3. **Datos de Aplicación**
   - Información que la aplicación necesita guardar
   - Historial, logs, estados, etc.

4. **Actualizaciones OTA (Over-The-Air)**
   - Permite actualizar el firmware remotamente
   - Requiere múltiples particiones de aplicación para alternar entre versiones

### Sistema de Particiones

La memoria flash se divide en **particiones**, cada una con un propósito específico. La tabla de particiones define:
- **Nombre**: Identificador de la partición
- **Tipo**: `app` (aplicación) o `data` (datos)
- **Subtipo**: Especifica el uso concreto (factory, ota_0, nvs, phy, etc.)
- **Offset**: Dirección de inicio en la flash
- **Tamaño**: Espacio asignado a la partición

La tabla de particiones se almacena en los primeros bytes de la flash y es leída por el bootloader al arrancar.

### NVS (Non-Volatile Storage)

**NVS** es un sistema de almacenamiento clave-valor proporcionado por ESP-IDF para guardar datos de configuración de forma persistente.

#### Características de NVS:

- **Formato clave-valor**: Almacena pares clave-valor, similar a un diccionario
- **Tipos de datos soportados**: Strings, enteros (8, 16, 32, 64 bits), blobs binarios
- **Namespaces**: Permite organizar datos en espacios de nombres lógicos
- **Wear leveling**: Distribuye las escrituras para prolongar la vida útil de la flash
- **Integridad**: Incluye checksums para detectar corrupción

#### Uso en este Proyecto:

Este proyecto utiliza **dos particiones NVS**:

1. **`nvs`** (pequeña, ~24-144KB):
   - Almacena configuración del sistema ESP-IDF
   - Datos de Wi-Fi del framework
   - Configuración de PHY (capa física de radio)

2. **`nvs_settings`** (grande, ~184KB-1.99MB):
   - Almacena la configuración específica de la aplicación
   - Parámetros como:
     - Credenciales Wi-Fi (SSID, contraseña)
     - Configuración MQTT (servidor, puerto, usuario, contraseña, certificados)
     - Identificadores (estación, experimento, dispositivo)
     - Configuración NTP (servidor de tiempo)
   - Se genera desde un archivo CSV usando `nvs_partition_generator`
   - Permite configurar cada dispositivo de forma individual

#### Carga de Configuración:

El sistema intenta cargar la configuración en este orden:
1. **SD Card**: Busca `nmda.ini` en la tarjeta SD (si está disponible)
2. **NVS**: Lee desde la partición `nvs_settings`
3. **Valores por defecto**: Si no encuentra configuración, usa valores predefinidos

### Tipos de Particiones en este Proyecto

#### Particiones de Aplicación (`app`):

- **`factory`**: Firmware de fábrica/respaldo
  - Siempre disponible como respaldo
  - Se puede volver a esta versión si hay problemas

- **`ota_0` y `ota_1`**: Particiones para actualizaciones OTA
  - Permiten tener dos versiones del firmware
  - El bootloader alterna entre ellas según `otadata`
  - Una contiene la versión activa, la otra se usa para actualizar

#### Particiones de Datos (`data`):

- **`nvs`**: Configuración del sistema ESP-IDF
  - Wi-Fi, PHY, y otros parámetros del framework

- **`otadata`**: Metadatos de OTA
  - Indica qué partición OTA está activa
  - Estado de las actualizaciones

- **`phy_init`**: Parámetros de inicialización de la radio
  - Configuración de la capa física (PHY) de Wi-Fi/Bluetooth
  - Calibración específica del hardware

- **`nvs_settings`**: Configuración de la aplicación
  - Todos los parámetros específicos del sistema NMDA
  - Se genera desde CSV y se flashea por separado

### Bootloader y Tabla de Particiones

El **bootloader** es el primer código que se ejecuta al arrancar el ESP32:

1. Se ejecuta desde el inicio de la flash (offset 0x0)
2. Lee la tabla de particiones (almacenada alrededor de 0x8000)
3. Determina qué partición de aplicación ejecutar:
   - Verifica `otadata` para ver si hay una partición OTA activa
   - Si no, ejecuta la partición `factory`
4. Carga y ejecuta la aplicación correspondiente

El bootloader también contiene información sobre el tamaño de flash y el modo de acceso (DIO, QIO, etc.) en su header, que debe coincidir con el hardware real.

### ¿Qué significan DIO, QIO, y otros modos de acceso a la flash?

Al programar el ESP32, a menudo verás referencias a modos como **DIO**, **QIO**, **DOUT** y **QOUT** en la configuración de la memoria flash. Estos modos especifican cómo el microcontrolador se comunica físicamente con el chip de memoria flash externo.

- **DIO (Dual I/O)**  
  Usa dos líneas de datos (IO0 y IO1). Permite transferir dos bits por ciclo. Es más rápido que el modo estándar (normalmente llamado "Standard" o "Single I/O") pero más compatible que QIO.

- **QIO (Quad I/O)**  
  Utiliza cuatro líneas de datos (IO0, IO1, IO2, IO3), transfiriendo cuatro bits por ciclo. Es el modo más rápido y recomendado para la mayoría de los ESP32 modernos **si el chip flash lo soporta**.

- **DOUT (Dual Output)**  
  Similar a DIO, pero las señales de reloj y comando son distintas. Es más antiguo y menos habitual.

- **QOUT (Quad Output)**  
  Como QIO, pero la escritura de comandos es diferente. Menos frecuente en configuraciones modernas.

#### ¿Por qué importa?

- El modo debe coincidir entre la configuración de build, el bootloader y el hardware real.  
- Si seleccionas un modo no soportado por el chip de memoria flash, el ESP32 puede no iniciar correctamente.
- **QIO** es el más eficiente, pero si hay problemas de compatibilidad usa **DIO**, que es más universal.
- El modo elegido afecta principalmente la velocidad de arranque y escritura/lectura de la flash.

#### ¿Dónde se configura?

- En el archivo de configuración (`sdkconfig`) bajo la opción `CONFIG_ESPTOOLPY_FLASHMODE`
- Al compilar el bootloader y el firmware, así como al flashear usando esptool.py (`--flash_mode dio/qio/dout/qout`).

> **Consejo:** Si tienes problemas de arranque, prueba flashear usando `dio` en vez de `qio`.

**Ejemplo de comando:**
```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 write_flash --flash_mode dio 0x1000 bootloader.bin
```

**Resumen:**
- **DIO/QIO:** Dual/Quad I/O (2 o 4 líneas de datos), más rápido, preferido si es compatible.
- **DOUT/QOUT:** Modos legacy, usar solo si el hardware lo requiere.
- **Siempre consulta la hoja de datos de tu chip flash y la configuración de hardware del ESP32.**


### Consideraciones Importantes

- **Alineación**: Las particiones de aplicación deben estar alineadas a 64KB (0x10000)
- **Tamaño mínimo**: Cada partición debe ser lo suficientemente grande para el firmware compilado
- **Espacio libre**: Es recomendable dejar algo de margen para futuras actualizaciones
- **Wear leveling**: NVS gestiona automáticamente el desgaste, pero las particiones de aplicación se escriben menos frecuentemente
- **Backup**: La partición `factory` actúa como respaldo en caso de problemas con OTA

## Verificar Tamaño de Flash del ESP32

Antes de compilar, es importante verificar qué modelo de ESP32 tienes:

### Opción 1: Verificar con esptool (recomendado)
```bash
# Localmente (si tienes acceso directo)
python3 -m esptool --port /dev/ttyUSB0 flash_id

# En la máquina remota
python3 -m esptool --port /dev/ttyUSB0 flash_id
```

Esto mostrará el tamaño real del flash del ESP32.

### Opción 2: Usar el script de verificación
```bash
./check_flash_size.sh ogarcia@kid /dev/ttyUSB0
```

## Archivos de Configuración

### Para 4MB:
- `sdkconfig.defaults` - Configuración para 4MB
- `partitions/partitions_4mb.csv` - Partition table para 4MB

### Para 8MB:
- `sdkconfig.defaults.8mb` - Backup de configuración para 8MB
- `partitions/partitions_8mb.csv` - Backup de partition table para 8MB
- `partitions/partitions.csv` - Partition table para 8MB (usada cuando está configurado para 8MB)

## Cambiar entre 4MB y 8MB

### Usando el script (recomendado):
```bash
# Cambiar a 4MB
./switch_flash_size.sh 4mb

# Cambiar a 8MB
./switch_flash_size.sh 8mb
```

### Manualmente:

**Para 4MB:**
1. Asegúrate de que `sdkconfig.defaults` tenga:
   ```
   CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
   CONFIG_ESPTOOLPY_FLASHSIZE="4MB"
   CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/partitions_4mb.csv"
   ```

2. Usa `partitions/partitions_4mb.csv` como partition table

**Para 8MB:**
1. Restaura desde backup:
   ```bash
   cp sdkconfig.defaults.8mb sdkconfig.defaults
   cp partitions/partitions_8mb.csv partitions/partitions.csv
   ```

2. O modifica `sdkconfig.defaults` para tener:
   ```
   CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
   CONFIG_ESPTOOLPY_FLASHSIZE="8MB"
   CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/partitions.csv"
   ```

## Después de Cambiar la Configuración

Siempre que cambies el tamaño de flash:

1. **Eliminar configuración anterior:**
   ```bash
   rm -f sdkconfig sdkconfig.old
   ```

2. **Recompilar limpiamente:**
   ```bash
   idf.py fullclean
   idf.py build
   ```

3. **Verificar el header del bootloader** (opcional):
   ```bash
   python3 << 'EOF'
   with open('build/bootloader/bootloader.bin', 'rb') as f:
       header = f.read(16)
       byte3 = header[3]
       flash_size_code = (byte3 >> 3) & 0x1F
       flash_mode = byte3 & 0x07
       sizes = {0x4: '4MB', 0x5: '8MB', 0x6: '16MB'}
       modes = {0: 'QIO', 1: 'QOUT', 2: 'DIO', 3: 'DOUT'}
       print(f'Byte 3: 0x{byte3:02x}')
       print(f'Flash size code: 0x{flash_size_code:x}')
       print(f'Flash size configurado: {sizes.get(flash_size_code, "Unknown")}')
       print(f'Flash mode: {modes.get(flash_mode, "Unknown")}')
   EOF
   ```

El header del bootloader ESP32 tiene este formato:
- Byte 3: bits 0-2 = flash mode, bits 3-7 = flash size code
  - 0x4 = 4MB
  - 0x5 = 8MB  
  - 0x6 = 16MB

## Tablas de Particiones

Las tablas de particiones definen cómo se organiza la memoria flash. Este proyecto tiene dos tablas diferentes según el tamaño de flash disponible.

### 4MB (partitions_4mb.csv):
- `nvs`: 24KB (0x9000 - 0xF000)
- `otadata`: 8KB (0xF000 - 0x11000)
- `phy_init`: 4KB (0x11000 - 0x12000)
- `factory`: 1.875MB (0x12000 - 0x1F2000)
- `ota_0`: 1.875MB (0x1F2000 - 0x3D2000)
- `nvs_settings`: 184KB (0x3D2000 - 0x400000)

**Total usado:** 4MB exactos

### 8MB (partitions.csv):
- `nvs`: 144KB (0x9000 - 0x2D000)
- `otadata`: 8KB (0x2D000 - 0x2F000)
- `phy_init`: 4KB (0x2F000 - 0x30000)
- `factory`: 2MB (0x30000 - 0x230000)
- `ota_0`: 2MB (0x230000 - 0x430000)
- `ota_1`: 2MB (0x430000 - 0x630000)
- `nvs_settings`: 1.99MB (0x630000 - 0x7FC000)

**Total usado:** ~8MB

### Notas sobre las Tablas de Particiones

- **Alineación**: Las particiones de tipo `app` deben estar alineadas a 64KB (0x10000)
- **Espacio reservado**: Los primeros 0x9000 bytes están reservados para el bootloader y la tabla de particiones
- **Diferencias 4MB vs 8MB**: 
  - En 4MB no hay partición `ota_1` (solo `ota_0` y `factory`)
  - El tamaño de `nvs_settings` es mucho menor en 4MB (184KB vs 1.99MB)
  - Los offsets de `factory` son diferentes para optimizar el espacio

Para más información sobre cómo generar y flashear la partición `nvs_settings`, consulta [`partitions/README_NVS.md`](partitions/README_NVS.md).

## Offset de Factory

El offset de la partición `factory` cambia según el tamaño de flash:

- **4MB**: `0x12000` (partitions_4mb.csv)
- **8MB**: `0x30000` (partitions.csv)

El script `deploy_remote.sh` detecta automáticamente el tamaño y usa el offset correcto.

## Troubleshooting

### Error: "Detected size smaller than binary image header"

Este error ocurre cuando:
- El firmware está compilado para un tamaño (ej: 8MB)
- Pero el ESP32 tiene un tamaño diferente (ej: 4MB)

**Solución**:
1. Verifica el tamaño real del ESP32: `./check_flash_size.sh ogarcia@kid /dev/ttyUSB0`
2. Configura el proyecto para el tamaño correcto: `./switch_flash_size.sh [4mb|8mb]`
3. Recompila limpiamente: `rm -f sdkconfig sdkconfig.old && idf.py fullclean && idf.py build`

### El bootloader no refleja el tamaño correcto después de cambiar la configuración

Si después de cambiar la configuración el bootloader no se compila con el tamaño correcto:

1. Asegúrate de haber eliminado `sdkconfig` y `sdkconfig.old` antes de recompilar
2. Haz una recompilación limpia: `idf.py fullclean && idf.py build`
3. Verifica el header del bootloader compilado como se muestra en la sección "Después de Cambiar la Configuración"

**Nota**: Si la compilación se hace correctamente con la configuración adecuada, el bootloader debería compilarse automáticamente con el tamaño correcto. No debería ser necesario corregir manualmente el header del bootloader.

Si el problema persiste, puede ser necesario verificar que no haya configuraciones conflictivas en `sdkconfig.defaults`.
