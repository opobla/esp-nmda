# Despliegue Remoto ESP32

Este documento describe cómo compilar el firmware localmente y desplegarlo en un ESP32 conectado a una máquina remota accesible por VPN.

## Requisitos

### Máquina Local (desarrollo)
- ESP-IDF instalado y configurado
- Acceso SSH a la máquina remota
- `scp` disponible

### Máquina Remota (Linux Debian)
- **Python 3.x** (requerido para `esptool.py`)
- Dispositivo ESP32 conectado (típicamente `/dev/ttyUSB0`)
- Acceso SSH habilitado
- `screen` o `minicom` para monitoreo serial
- **NO se requiere ESP-IDF instalado** - el script transfiere `esptool.py` automáticamente

## Configuración Inicial

### 1. Configurar acceso SSH sin contraseña (opcional pero recomendado)

En tu máquina local:
```bash
ssh-keygen -t rsa -b 4096
ssh-copy-id -p [PORT] user@remote-host
```

### 2. Verificar dispositivo en máquina remota

Conecta el ESP32 y verifica el dispositivo:
```bash
ssh user@remote-host "ls -l /dev/ttyUSB*"
```

## Uso

### Desplegar firmware (compilar + flashear)

```bash
./deploy_remote.sh user@remote-host /dev/ttyUSB0
```

O con puerto SSH personalizado:
```bash
./deploy_remote.sh user@remote-host 2222 /dev/ttyUSB0
```

El script:
1. Compila el proyecto localmente
2. Transfiere los binarios necesarios por SSH
3. Flashea el ESP32 remotamente
4. Limpia archivos temporales

### Monitorear salida serial

```bash
./monitor_remote.sh user@remote-host /dev/ttyUSB0
```

O con puerto y baudrate personalizados:
```bash
./monitor_remote.sh user@remote-host 2222 /dev/ttyUSB0 115200
```

### Despliegue completo (compilar + flashear + monitorear)

```bash
./deploy_remote.sh user@remote-host /dev/ttyUSB0 && \
./monitor_remote.sh user@remote-host /dev/ttyUSB0
```

## Alternativa: Usar idf.py remotamente

Si prefieres usar `idf.py` directamente en la máquina remota:

### 1. Transferir solo el código fuente

```bash
# Crear tarball del código fuente (sin build/)
tar --exclude='build' --exclude='.git' -czf project.tar.gz .

# Transferir
scp project.tar.gz user@remote-host:/tmp/

# En remoto: extraer y compilar
ssh user@remote-host
cd /tmp
tar -xzf project.tar.gz
cd project
. $IDF_PATH/export.sh
idf.py build flash monitor
```

### 2. Usar rsync para sincronizar (más eficiente)

```bash
# Sincronizar código fuente (excluyendo build/)
rsync -avz --exclude 'build' --exclude '.git' \
    -e "ssh -p [PORT]" \
    ./ user@remote-host:/tmp/esp32-project/

# Compilar y flashear remotamente
ssh -p [PORT] user@remote-host "cd /tmp/esp32-project && \
    . \$IDF_PATH/export.sh && \
    idf.py build flash monitor"
```

## Solución de Problemas

### Error: "esptool.py no encontrado"
- **Ya no debería ocurrir**: El script transfiere `esptool.py` automáticamente
- Si ocurre, verifica que `IDF_PATH` esté configurado en tu máquina local
- Verifica que el directorio `$IDF_PATH/components/esptool_py` existe

### Error: "Python no encontrado"
- Instala Python 3 en la máquina remota: `sudo apt-get install python3`

### Error: "Dispositivo no encontrado"
- Verifica que el ESP32 esté conectado: `ls -l /dev/ttyUSB*`
- Verifica permisos: `sudo chmod 666 /dev/ttyUSB0`
- Añade tu usuario al grupo `dialout`: `sudo usermod -a -G dialout $USER`

### Error: "Permission denied" o "Device or resource busy"

**Solución temporal (el script usa sudo automáticamente):**
- El script detecta automáticamente si necesita sudo y lo usa

**Solución permanente:**

1. **Verificar que el usuario está en el grupo dialout:**
```bash
groups $USER
# Debe mostrar "dialout"
```

2. **Si no está, añadirlo:**
```bash
sudo usermod -a -G dialout $USER
```

3. **Cerrar sesión y volver a entrar** (los cambios de grupo requieren nuevo login)

4. **O crear regla udev permanente:**
```bash
sudo nano /etc/udev/rules.d/99-esp32.rules
# Añadir: SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", MODE="0666", GROUP="dialout"
sudo udevadm control --reload-rules
sudo udevadm trigger
```

5. **Verificar permisos después:**
```bash
ls -l /dev/ttyUSB0
# Debe mostrar: crw-rw-rw- o crw-rw---- con grupo dialout
```

### Puerto serial bloqueado
```bash
# Verificar procesos usando el puerto
lsof /dev/ttyUSB0
# Matar proceso si es necesario
kill -9 [PID]
```

## Optimización: Solo transferir cambios

Para proyectos grandes, puedes optimizar transfiriendo solo los binarios compilados:

```bash
# Script optimizado que solo transfiere si hay cambios
./deploy_remote.sh user@remote-host /dev/ttyUSB0 --fast
```

## Variables de Entorno

Puedes configurar valores por defecto creando un archivo `.deploy_config`:

```bash
REMOTE_HOST=user@remote-host
REMOTE_PORT=22
DEVICE=/dev/ttyUSB0
BAUDRATE=115200
```

Y usar:
```bash
source .deploy_config
./deploy_remote.sh
```

