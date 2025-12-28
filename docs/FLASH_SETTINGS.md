# Flasheo de Settings NVS

Este documento explica cómo flashear la partición NVS con la configuración del sistema usando los archivos CSV.

## Tabla de Contenidos

1. [Archivos CSV Disponibles](#archivos-csv-disponibles)
2. [Flasheo Automático con Makefile](#flasheo-automático-con-makefile)
3. [Flasheo Manual](#flasheo-manual)
4. [Verificación](#verificación)
5. [Troubleshooting](#troubleshooting)

## Archivos CSV Disponibles

En el directorio `partitions/` hay varios archivos CSV preconfigurados para diferentes dispositivos:

| Archivo CSV | Descripción | Uso |
|-------------|------------|-----|
| `settings.csv` | Configuración por defecto (HiveMQ Cloud) | Dispositivo genérico |
| `settings.op64hive.csv` | Configuración para op64hive (HiveMQ Cloud) | Dispositivo op64hive |
| `settings.op64.csv` | Configuración para op64 (MQTT local) | Dispositivo op64 |
| `settings.orca.csv` | Configuración para ORCA | Dispositivo ORCA |
| `settings.icaro.csv` | Configuración para ICaRO | Dispositivo ICaRO |
| `settings.icaro2.csv` | Configuración para ICaRO 2 | Dispositivo ICaRO 2 |
| `settings.sample.csv` | Plantilla de ejemplo | Referencia/documentación |

## Flasheo Automático con Makefile

El Makefile permite flashear la configuración de forma sencilla, detectando automáticamente el tamaño de flash y permitiendo especificar qué archivo CSV usar.

### Uso Básico (CSV por defecto)

Para usar el archivo `settings.csv` por defecto:

```bash
cd partitions
make flash
```

Esto:
1. Detecta automáticamente el tamaño de flash (4MB o 8MB) desde `sdkconfig`
2. Genera `settings.bin` desde `settings.csv`
3. Muestra un resumen del contenido
4. Flashea la partición NVS al offset correcto

### Especificar Archivo CSV

Para usar un archivo CSV específico, pasa la variable `CSV`:

```bash
cd partitions

# Usar settings.op64.csv
make flash CSV=settings.op64.csv

# Usar settings.op64hive.csv
make flash CSV=settings.op64hive.csv

# Usar settings.orca.csv
make flash CSV=settings.orca.csv
```

**Nota**: El archivo binario generado tendrá el mismo nombre base que el CSV. Por ejemplo:
- `settings.op64.csv` → genera `settings.op64.bin`
- `settings.orca.csv` → genera `settings.orca.bin`

### Forzar Tamaño de Flash

Si quieres forzar un tamaño específico independientemente de la detección automática:

```bash
# Para 4MB flash
make flash-4mb CSV=settings.op64.csv

# Para 8MB flash
make flash-8mb CSV=settings.op64hive.csv
```

### Ejemplos Completos

```bash
# Flashear configuración por defecto (settings.csv) en dispositivo 4MB
cd partitions
make flash-4mb

# Flashear configuración de op64hive en dispositivo 8MB
cd partitions
make flash-8mb CSV=settings.op64hive.csv

# Flashear configuración de ORCA (detección automática de tamaño)
cd partitions
make flash CSV=settings.orca.csv
```

## Flasheo Manual

Si prefieres hacer el proceso manualmente o necesitas más control:

### Paso 1: Generar el Binario

```bash
cd partitions

# Para 4MB flash
${IDF_PATH}/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py \
    generate settings.op64.csv settings.op64.bin 131072

# Para 8MB flash
${IDF_PATH}/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py \
    generate settings.op64hive.csv settings.op64hive.bin 1884160
```

**Parámetros**:
- Primer argumento: archivo CSV de entrada
- Segundo argumento: archivo binario de salida
- Tercer argumento: tamaño de la partición NVS en bytes
  - 4MB: `131072` (0x20000)
  - 8MB: `1884160` (0x1CC000)

### Paso 2: Verificar el Contenido (Opcional)

Antes de flashear, puedes verificar el contenido del binario:

```bash
python ${IDF_PATH}/components/nvs_flash/nvs_partition_tool/nvs_tool.py -i settings.op64.bin
```

Esto mostrará todas las claves y valores que se flashearán.

### Paso 3: Flashear la Partición NVS

```bash
# Para 4MB flash (offset: 0x3E0000)
esptool.py write_flash 0x3E0000 settings.op64.bin

# Para 8MB flash (offset: 0x630000)
esptool.py write_flash 0x630000 settings.op64hive.bin
```

## Verificación

Después de flashear, reinicia el dispositivo y verifica en los logs que las claves se cargaron correctamente:

### Verificación Exitosa

Deberías ver mensajes como:

```
I (xxxx) SETTINGS: Loaded wifi_ssid: Virus 24g
I (xxxx) SETTINGS: Loaded wifi_password: tu_contraseña
I (xxxx) SETTINGS: Loaded mqtt_host: 192.168.1.5
I (xxxx) SETTINGS: Loaded mqtt_station: op64
I (xxxx) SETTINGS: Loaded mqtt_experiment: cosmic_rays
I (xxxx) SETTINGS: Loaded mqtt_device_id: op64hive
```

### Problemas Comunes

**Clave no encontrada**:
```
W (xxxx) SETTINGS: Key 'wifi_password' not found in NVS
W (xxxx) SETTINGS: Failed to load wifi_password from NVS, keeping default
```

**Solución**: Verifica que:
1. El CSV tiene la clave correcta (ej: `wifi_password`, no `wifi_pasword`)
2. La partición NVS fue flasheada correctamente
3. El offset y tamaño coinciden con la partición definida en `partitions.csv`

**Partición no encontrada**:
```
W (xxxx) SETTINGS: NVS partition 'nvs_settings' not found or not initialized
```

**Solución**: Verifica que:
1. La partición `nvs_settings` está definida en `partitions.csv`
2. El offset y tamaño en `partitions.csv` coinciden con los usados para flashear

## Troubleshooting

### El Makefile no detecta el tamaño de flash correctamente

Si la detección automática falla, puedes forzar el tamaño:

```bash
# Forzar 4MB
make flash-4mb CSV=settings.op64.csv

# Forzar 8MB
make flash-8mb CSV=settings.op64hive.csv
```

### Error: "File not found" al usar CSV personalizado

Asegúrate de que el archivo CSV existe en el directorio `partitions/`:

```bash
cd partitions
ls -la settings.*.csv  # Lista todos los CSV disponibles
```

### Error: "Partition size mismatch"

El tamaño del binario debe coincidir exactamente con el tamaño de la partición NVS definida en `partitions.csv`. Verifica:

1. El tamaño en `partitions.csv` para la partición `nvs_settings`
2. El tamaño usado al generar el binario (131072 para 4MB, 1884160 para 8MB)

### Limpiar Archivos Generados

Para eliminar todos los archivos `.bin` generados:

```bash
cd partitions
make clean
```

Esto elimina todos los archivos `*.bin` del directorio.

## Referencias

- **Makefile**: `partitions/Makefile` - Contiene las reglas de flasheo
- **Configuración NVS**: Ver `docs/NVS_CONFIGURATION.md` para detalles sobre las claves disponibles
- **Particiones**: `partitions/partitions.csv` - Define las particiones del sistema

## Resumen de Comandos Rápidos

```bash
# Flashear con CSV por defecto (settings.csv)
cd partitions && make flash

# Flashear con CSV específico
cd partitions && make flash CSV=settings.op64.csv

# Flashear forzando tamaño 4MB
cd partitions && make flash-4mb CSV=settings.op64.csv

# Flashear forzando tamaño 8MB
cd partitions && make flash-8mb CSV=settings.op64hive.csv

# Limpiar archivos generados
cd partitions && make clean
```
