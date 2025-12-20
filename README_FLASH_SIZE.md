# Configuración de Tamaño de Flash

Este proyecto soporta ESP32 con **4MB** o **8MB** de flash. La configuración se puede cambiar fácilmente usando el script `switch_flash_size.sh`.

## Archivos de Configuración

### Para 4MB:
- `sdkconfig.defaults` - Configuración para 4MB (actual)
- `partitions/partitions_4mb.csv` - Partition table para 4MB

### Para 8MB (backup):
- `sdkconfig.defaults.8mb` - Backup de configuración para 8MB
- `partitions/partitions_8mb.csv` - Backup de partition table para 8MB

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

3. **Verificar el header del bootloader:**
   ```bash
   python3 fix_bootloader_header.py build/bootloader/bootloader.bin
   ```

## Partition Tables

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

## Offset de Factory

El offset de la partición `factory` cambia según el tamaño de flash:

- **4MB**: `0x12000` (partitions_4mb.csv)
- **8MB**: `0x30000` (partitions.csv)

El script `deploy_remote.sh` detecta automáticamente el tamaño y usa el offset correcto.

## Verificar Tamaño de Flash del ESP32

Para verificar el tamaño real del flash de tu ESP32:

```bash
# Localmente (si tienes acceso directo)
python3 -m esptool --port /dev/ttyUSB0 flash_id

# Remotamente
./check_flash_size.sh ogarcia@kid /dev/ttyUSB0
```

## Troubleshooting

### Error: "Detected size smaller than binary image header"
- Verifica que el tamaño configurado coincida con el hardware
- Usa `./check_flash_size.sh` para verificar el tamaño real
- Asegúrate de haber recompilado después de cambiar la configuración

### Bootloader con tamaño incorrecto
- Ejecuta `python3 fix_bootloader_header.py build/bootloader/bootloader.bin`
- O usa `./rebuild_fix_flash.sh` que lo corrige automáticamente

