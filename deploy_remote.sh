#!/bin/bash

# Script para compilar localmente y desplegar remotamente en ESP32
# Uso: ./deploy_remote.sh [user@]hostname [port] [device]
#
# Ejemplo: ./deploy_remote.sh user@remote-host /dev/ttyUSB0
#          ./deploy_remote.sh user@remote-host 2222 /dev/ttyUSB0

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Verificar argumentos
if [ $# -lt 1 ]; then
    echo -e "${RED}Error: Se requiere al menos el hostname${NC}"
    echo "Uso: $0 [user@]hostname [port] [device]"
    echo "Ejemplo: $0 user@remote-host /dev/ttyUSB0"
    echo "         $0 user@remote-host 2222 /dev/ttyUSB0"
    exit 1
fi

REMOTE_HOST="$1"
REMOTE_PORT="22"
DEVICE="/dev/ttyUSB0"

# Parsear argumentos restantes
if [ $# -ge 2 ]; then
    # Si el segundo argumento es un número, es el puerto
    if [[ "$2" =~ ^[0-9]+$ ]]; then
        REMOTE_PORT="$2"
        # El tercer argumento sería el dispositivo
        if [ $# -ge 3 ]; then
            DEVICE="$3"
        fi
    else
        # El segundo argumento es el dispositivo (sin puerto especificado)
        DEVICE="$2"
    fi
fi

# Extraer usuario y hostname
if [[ $REMOTE_HOST == *"@"* ]]; then
    REMOTE_USER=$(echo $REMOTE_HOST | cut -d'@' -f1)
    REMOTE_HOSTNAME=$(echo $REMOTE_HOST | cut -d'@' -f2)
else
    REMOTE_USER=$(whoami)
    REMOTE_HOSTNAME=$REMOTE_HOST
fi

echo -e "${GREEN}=== Deploy Remoto ESP32 ===${NC}"
echo "Host remoto: ${REMOTE_USER}@${REMOTE_HOSTNAME}:${REMOTE_PORT}"
echo "Dispositivo: ${DEVICE}"
echo ""

# Verificar que estamos en el directorio del proyecto
if [ ! -f "CMakeLists.txt" ] || [ ! -d "main" ]; then
    echo -e "${RED}Error: Este script debe ejecutarse desde el directorio raíz del proyecto${NC}"
    exit 1
fi

# Verificar que ESP-IDF está configurado
if [ -z "$IDF_PATH" ]; then
    echo -e "${RED}Error: IDF_PATH no está configurado${NC}"
    echo "Ejecuta: . \$IDF_PATH/export.sh"
    exit 1
fi

# Buscar directorio esptool_py
ESPTOOL_DIR=""
if [ -d "$IDF_PATH/components/esptool_py" ]; then
    ESPTOOL_DIR="$IDF_PATH/components/esptool_py"
elif [ -d "$HOME/esp/esp-idf/components/esptool_py" ]; then
    ESPTOOL_DIR="$HOME/esp/esp-idf/components/esptool_py"
else
    echo -e "${RED}Error: No se pudo encontrar esptool_py${NC}"
    echo "Busca en: \$IDF_PATH/components/esptool_py"
    exit 1
fi

echo "Usando esptool desde: $ESPTOOL_DIR"

# Paso 1: Compilar
echo -e "${YELLOW}[1/4] Compilando proyecto...${NC}"
idf.py build
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: La compilación falló${NC}"
    exit 1
fi

# Paso 2: Crear directorio temporal en remoto
echo -e "${YELLOW}[2/4] Preparando directorio remoto...${NC}"
REMOTE_DIR="/tmp/esp32_deploy_$$"
ssh -p "$REMOTE_PORT" "${REMOTE_USER}@${REMOTE_HOSTNAME}" "mkdir -p ${REMOTE_DIR}"

# Paso 3: Transferir archivos necesarios
echo -e "${YELLOW}[3/4] Transfiriendo archivos...${NC}"

# Transferir archivos (ya no necesitamos esptool, se instala vía pip en remoto)
scp -P "$REMOTE_PORT" \
    build/bootloader/bootloader.bin \
    build/partition_table/partition-table.bin \
    build/main.bin \
    "${REMOTE_USER}@${REMOTE_HOSTNAME}:${REMOTE_DIR}/"

# Detectar tamaño de flash y offset de factory desde configuración local
FLASH_SIZE="4MB"
FACTORY_OFFSET="0x20000"  # Offset para 4MB (partitions_4mb.csv)
if [ -f "sdkconfig" ] && grep -q "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y" sdkconfig; then
    FLASH_SIZE="8MB"
    FACTORY_OFFSET="0x30000"  # Offset para 8MB (partitions.csv)
    echo "Detectado: 8MB flash (factory offset: ${FACTORY_OFFSET})"
elif [ -f "sdkconfig.defaults" ] && grep -q "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y" sdkconfig.defaults; then
    FLASH_SIZE="8MB"
    FACTORY_OFFSET="0x30000"  # Offset para 8MB (partitions.csv)
    echo "Detectado: 8MB flash (factory offset: ${FACTORY_OFFSET})"
else
    echo "Detectado: 4MB flash (factory offset: ${FACTORY_OFFSET})"
fi

# Paso 4: Flashear remotamente
echo -e "${YELLOW}[4/4] Flasheando ESP32 (${FLASH_SIZE})...${NC}"
ssh -p "$REMOTE_PORT" "${REMOTE_USER}@${REMOTE_HOSTNAME}" << EOF
    set -e
    
    # Verificar que el dispositivo existe
    if [ ! -e "${DEVICE}" ]; then
        echo -e "${RED}Error: Dispositivo ${DEVICE} no encontrado${NC}"
        exit 1
    fi

    # Verificar permisos del dispositivo
    if [ ! -r "${DEVICE}" ] || [ ! -w "${DEVICE}" ]; then
        echo -e "${YELLOW}Advertencia: Sin permisos de lectura/escritura en ${DEVICE}${NC}"
        echo "Intentando con sudo..."
        SUDO_CMD="sudo"
    else
        SUDO_CMD=""
    fi

    # Verificar que Python está disponible
    if ! command -v python3 &> /dev/null && ! command -v python &> /dev/null; then
        echo -e "${RED}Error: Python no encontrado. Se requiere Python 3.x${NC}"
        exit 1
    fi

    # Determinar comando Python
    PYTHON_CMD="python3"
    if ! command -v python3 &> /dev/null; then
        PYTHON_CMD="python"
    fi

    # Verificar si esptool está instalado, si no, instalarlo vía pip
    if ! \${PYTHON_CMD} -m esptool --help &> /dev/null; then
        echo "esptool no encontrado, instalando vía pip..."
        PIP_CMD=""
        if command -v pip3 &> /dev/null; then
            PIP_CMD="pip3"
        elif command -v pip &> /dev/null; then
            PIP_CMD="pip"
        else
            echo -e "${RED}Error: pip no encontrado. Instala pip o esptool manualmente${NC}"
            exit 1
        fi
        
        # Instalar esptool (versión compatible con ESP-IDF)
        \${PIP_CMD} install --user esptool 2>&1 | grep -v "already satisfied" || true
        
        # Verificar que se instaló correctamente
        if ! \${PYTHON_CMD} -m esptool --help &> /dev/null; then
            echo -e "${RED}Error: No se pudo instalar o usar esptool${NC}"
            exit 1
        fi
    fi

    ESPTOOL_CMD="\${PYTHON_CMD} -m esptool"

    # Flashear (usar sudo si es necesario)
    # Nota: El offset de factory cambia según el tamaño de flash:
    # - 4MB: 0x12000 (partitions_4mb.csv)
    # - 8MB: 0x30000 (partitions.csv)
    echo "Flasheando con tamaño: ${FLASH_SIZE}, offset factory: ${FACTORY_OFFSET}"
    \${SUDO_CMD} \${ESPTOOL_CMD} --chip esp32 --port ${DEVICE} --baud 921600 \\
        --before default_reset --after hard_reset write_flash \\
        --flash_mode dio --flash_freq 40m --flash_size ${FLASH_SIZE} \\
        0x1000 ${REMOTE_DIR}/bootloader.bin \\
        0x8000 ${REMOTE_DIR}/partition-table.bin \\
        ${FACTORY_OFFSET} ${REMOTE_DIR}/main.bin

    # Limpiar
    rm -rf ${REMOTE_DIR}
EOF

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Firmware flasheado exitosamente${NC}"
    echo ""
    echo -e "${YELLOW}Para monitorear la salida serial:${NC}"
    echo "ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOSTNAME} 'screen ${DEVICE} 115200'"
    echo "o"
    echo "ssh -p ${REMOTE_PORT} ${REMOTE_USER}@${REMOTE_HOSTNAME} 'minicom -D ${DEVICE} -b 115200'"
else
    echo -e "${RED}Error: El flasheo falló${NC}"
    exit 1
fi

