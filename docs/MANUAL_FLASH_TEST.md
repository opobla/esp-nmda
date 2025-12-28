# Prueba Manual de Flasheo Remoto

## 1. Verificar qué está usando el puerto

```bash
# En la máquina remota, verificar qué proceso está usando el puerto
lsof /dev/ttyUSB0
# O
fuser /dev/ttyUSB0
# O
ps aux | grep ttyUSB0
```

## 2. Liberar el puerto si está ocupado

```bash
# Si hay un proceso usando el puerto, terminarlo
# Ejemplo si es screen:
pkill -f "screen.*ttyUSB0"
# O si es minicom:
pkill -f "minicom.*ttyUSB0"
# O si es otro proceso, usar el PID que muestra lsof:
kill <PID>
```

## 3. Verificar permisos del dispositivo

```bash
ls -l /dev/ttyUSB0
# Debe mostrar algo como: crw-rw---- 1 root dialout 188, 0 dic 19 19:10 /dev/ttyUSB0
# Si no tienes permisos, usar sudo
```

## 4. Probar flasheo manualmente

### Opción A: Con esptool instalado localmente en la máquina remota

```bash
# En la máquina remota, navegar al directorio donde están los binarios
cd ~/esp32_flash_temp  # o el directorio que uses

# Verificar que esptool está instalado
python3 -m esptool --version

# Flashear manualmente (ajustar rutas según corresponda)
# Nota: El offset 0x30000 corresponde a la partición 'factory' según partitions.csv
# Forzamos --flash_size 8MB porque la detección automática puede fallar
sudo python3 -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
    --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_freq 40m --flash_size 8MB \
    0x1000 bootloader.bin \
    0x8000 partition-table.bin \
    0x30000 main.bin
```

### Opción B: Transferir binarios y flashear manualmente

```bash
# En tu máquina local, compilar y transferir binarios
idf.py build
scp -P 2222 build/bootloader/bootloader.bin ogarcia@icaro:~/esp32_flash_temp/
scp -P 2222 build/partition_table/partition-table.bin ogarcia@icaro:~/esp32_flash_temp/
scp -P 2222 build/main.bin ogarcia@icaro:~/esp32_flash_temp/

# En la máquina remota, instalar esptool si no está
pip3 install --user esptool

# Flashear
# Nota: El offset 0x30000 corresponde a la partición 'factory' según partitions.csv
# Forzamos --flash_size 8MB porque la detección automática puede fallar
sudo ~/.local/bin/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
    --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_freq 40m --flash_size 8MB \
    0x1000 ~/esp32_flash_temp/bootloader.bin \
    0x8000 ~/esp32_flash_temp/partition-table.bin \
    0x30000 ~/esp32_flash_temp/main.bin
```

## 5. Verificar que el dispositivo está libre antes de flashear

```bash
# Comando útil para verificar y liberar el puerto
sudo fuser -k /dev/ttyUSB0  # Mata procesos usando el puerto
# Esperar un segundo
sleep 1
# Verificar que está libre
lsof /dev/ttyUSB0  # No debe mostrar nada
```

## 6. Monitorear después del flasheo

```bash
# Opción 1: Con screen
sudo screen /dev/ttyUSB0 115200
# Salir: Ctrl+A luego K, luego Y

# Opción 2: Con minicom
sudo minicom -D /dev/ttyUSB0 -b 115200
# Salir: Ctrl+A luego X

# Opción 3: Con esptool monitor
sudo python3 -m esptool --port /dev/ttyUSB0 monitor
# Salir: Ctrl+]
```

## 7. Script rápido para verificar y liberar puerto

Crea este script en la máquina remota (`~/check_port.sh`):

```bash
#!/bin/bash
PORT="/dev/ttyUSB0"

echo "Verificando puerto $PORT..."
echo ""

# Verificar si existe
if [ ! -e "$PORT" ]; then
    echo "ERROR: Puerto $PORT no existe"
    exit 1
fi

# Verificar permisos
echo "Permisos:"
ls -l "$PORT"
echo ""

# Verificar qué lo está usando
echo "Procesos usando el puerto:"
lsof "$PORT" 2>/dev/null || echo "Ningún proceso encontrado"
echo ""

# Preguntar si quiere liberarlo
read -p "¿Liberar el puerto? (s/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Ss]$ ]]; then
    echo "Liberando puerto..."
    sudo fuser -k "$PORT" 2>/dev/null
    sleep 1
    echo "Puerto liberado"
    lsof "$PORT" 2>/dev/null || echo "Confirmado: puerto libre"
fi
```

Hacer ejecutable:
```bash
chmod +x ~/check_port.sh
```

## 8. Solución permanente: Regla udev

Para evitar problemas de permisos en el futuro:

```bash
# Crear regla udev
sudo nano /etc/udev/rules.d/99-esp32.rules
```

Añadir esta línea:
```
SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", MODE="0666", GROUP="dialout"
```

Aplicar cambios:
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
# Desconectar y reconectar el dispositivo USB
```

## Troubleshooting

### Error: "Device or resource busy"
- Verificar con `lsof /dev/ttyUSB0` qué proceso lo está usando
- Matar el proceso: `sudo fuser -k /dev/ttyUSB0`
- Si es screen/minicom, cerrarlo correctamente antes

### Error: "Permission denied"
- Usar `sudo` para el comando de flasheo
- O verificar que el usuario está en el grupo `dialout` y hacer logout/login

### Error: "Port not found"
- Verificar que el dispositivo está conectado: `lsusb | grep -i esp`
- Verificar que el dispositivo aparece: `ls -l /dev/ttyUSB*`
- Puede ser que necesite otro puerto: `/dev/ttyUSB1`, `/dev/ttyACM0`, etc.

