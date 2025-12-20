# Flasheo de Partición NVS (settings)

La partición `nvs_settings` almacena la configuración del sistema. El offset y tamaño cambian según el tamaño de flash del ESP32.

## Offsets según tamaño de flash

### 4MB Flash
- **Offset**: `0x3E0000`
- **Tamaño**: `0x20000` (131072 bytes = 128 KB)
- **Comando**:
  ```bash
  esptool.py write_flash 0x3E0000 settings.bin
  ```

### 8MB Flash
- **Offset**: `0x630000`
- **Tamaño**: `0x1CC000` (1884160 bytes = ~1.99 MB)
- **Comando**:
  ```bash
  esptool.py write_flash 0x630000 settings.bin
  ```

## Uso del Makefile

El Makefile detecta automáticamente el tamaño de flash desde `sdkconfig` o `sdkconfig.defaults`:

```bash
cd partitions
make flash
```

Esto generará `settings.bin` con el tamaño correcto y lo flasheará al offset correcto.

## Flasheo manual por tamaño

Si quieres forzar un tamaño específico:

```bash
# Para 4MB
make flash-4mb

# Para 8MB
make flash-8mb
```

## Generar settings.bin

1. Edita `settings.csv` con tus valores
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

## Verificar partición NVS

Para verificar que la partición existe y está correctamente flasheada:

```bash
# Listar particiones
python ${IDF_PATH}/components/partition_table/gen_esp32part.py \
    build/partition_table/partition-table.bin

# Deberías ver nvs_settings con el offset correcto
```

## Notas

- El tamaño del binario debe coincidir exactamente con el tamaño de la partición
- Si el binario es más grande que la partición, el flasheo fallará
- Si el binario es más pequeño, funcionará pero no usará todo el espacio disponible
- Después de flashear, el ESP32 debería poder leer la configuración desde NVS

