# Solución al Problema de Tamaño de Flash

## Problema
El ESP32 detecta 4MB de flash, pero el firmware está compilado para 8MB, causando el error:
```
E (590) spi_flash: Detected size(4096k) smaller than the size in the binary image header(8192k). Probe failed.
```

## Verificación del Tamaño Real del Flash

### Opción 1: Verificar con esptool (recomendado)
```bash
# En la máquina remota
python3 -m esptool --port /dev/ttyUSB0 flash_id
```

Esto mostrará el tamaño real del flash detectado.

### Opción 2: Usar el script de verificación
```bash
./check_flash_size.sh ogarcia@kid /dev/ttyUSB0
```

## Soluciones

### Si el ESP32 tiene 8MB (pero la detección falla)

El problema es que la detección automática falla. Soluciones:

1. **Forzar el tamaño en esptool** (ya implementado en deploy_remote.sh):
   - El script ya usa `--flash_size 8MB` para forzar el tamaño

2. **Verificar que el bootloader se compile correctamente**:
   ```bash
   # Limpiar y recompilar
   idf.py fullclean
   idf.py build
   
   # Verificar el header del bootloader
   python3 -c "
   with open('build/bootloader/bootloader.bin', 'rb') as f:
       header = f.read(16)
       byte3 = header[3]
       flash_size_code = (byte3 >> 3) & 0x1F
       sizes = {0x4: '4MB', 0x5: '8MB', 0x6: '16MB'}
       print(f'Flash size en header: {sizes.get(flash_size_code, \"Unknown\")}')
   "
   ```

3. **Si el problema persiste, forzar en el código**:
   - Añadir `CONFIG_ESPTOOLPY_FLASHSIZE="8MB"` explícitamente en `sdkconfig.defaults`
   - Verificar que no haya otras configuraciones que lo sobrescriban

### Si el ESP32 realmente tiene 4MB

Necesitas crear una partition table para 4MB. Ya existe `partitions/partitions_4mb.csv` (si se creó anteriormente) o crear una nueva.

**IMPORTANTE**: Si cambias a 4MB, necesitas:
1. Cambiar `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` a `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` en `sdkconfig.defaults`
2. Actualizar `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` para usar la partition table de 4MB
3. Recompilar completamente: `idf.py fullclean && idf.py build`

## Verificación Actual

Tu partition table actual termina en `0x7FC000`, que requiere 8MB:
- ✅ Cabe en 8MB (termina antes de 0x800000)
- ❌ NO cabe en 4MB (termina después de 0x400000)

## Pasos Recomendados

1. **Verificar el tamaño real del flash**:
   ```bash
   ./check_flash_size.sh ogarcia@kid /dev/ttyUSB0
   ```

2. **Si tiene 8MB pero sigue fallando**:
   - Verificar que `sdkconfig` tenga `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`
   - Hacer `idf.py fullclean && idf.py build`
   - Verificar el header del bootloader compilado

3. **Si realmente tiene 4MB**:
   - Crear/adjustar partition table para 4MB
   - Cambiar configuración a 4MB
   - Recompilar

## Debug del Header del Bootloader

El header del bootloader ESP32 tiene este formato:
- Byte 3: bits 0-2 = flash mode, bits 3-7 = flash size code
  - 0x4 = 4MB
  - 0x5 = 8MB  
  - 0x6 = 16MB

Para verificar:
```bash
python3 << 'EOF'
with open('build/bootloader/bootloader.bin', 'rb') as f:
    header = f.read(16)
    byte3 = header[3]
    flash_size_code = (byte3 >> 3) & 0x1F
    sizes = {0x4: '4MB', 0x5: '8MB', 0x6: '16MB'}
    print(f'Byte 3: 0x{byte3:02x}')
    print(f'Flash size code: 0x{flash_size_code:x}')
    print(f'Flash size configurado: {sizes.get(flash_size_code, "Unknown")}')
EOF
```

