#!/bin/bash

# Script para monitorear la salida serial del ESP32 remotamente
# Uso: ./monitor_remote.sh [user@]hostname [port] [device] [baudrate]
#
# Ejemplo: ./monitor_remote.sh user@remote-host /dev/ttyUSB0
#          ./monitor_remote.sh user@remote-host 2222 /dev/ttyUSB0 115200

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Verificar argumentos
if [ $# -lt 1 ]; then
    echo -e "${RED}Error: Se requiere al menos el hostname${NC}"
    echo "Uso: $0 [user@]hostname [port] [device] [baudrate]"
    echo "Ejemplo: $0 user@remote-host /dev/ttyUSB0"
    echo "         $0 user@remote-host 2222 /dev/ttyUSB0 115200"
    exit 1
fi

REMOTE_HOST="$1"
REMOTE_PORT="22"
DEVICE="/dev/ttyUSB0"
BAUDRATE="115200"

# Parsear argumentos restantes
if [ $# -ge 2 ]; then
    # Si el segundo argumento es un número, es el puerto
    if [[ "$2" =~ ^[0-9]+$ ]]; then
        REMOTE_PORT="$2"
        # El tercer argumento sería el dispositivo
        if [ $# -ge 3 ]; then
            DEVICE="$3"
            # El cuarto argumento sería el baudrate
            if [ $# -ge 4 ]; then
                BAUDRATE="$4"
            fi
        fi
    else
        # El segundo argumento es el dispositivo (sin puerto especificado)
        DEVICE="$2"
        # El tercer argumento sería el baudrate
        if [ $# -ge 3 ]; then
            BAUDRATE="$3"
        fi
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

echo -e "${GREEN}=== Monitor Remoto ESP32 ===${NC}"
echo "Host remoto: ${REMOTE_USER}@${REMOTE_HOSTNAME}:${REMOTE_PORT}"
echo "Dispositivo: ${DEVICE}"
echo "Baudrate: ${BAUDRATE}"
echo ""
echo -e "${YELLOW}Presiona Ctrl+A luego K para salir de screen${NC}"
echo ""

# Verificar permisos y usar sudo si es necesario
ssh -t -p "$REMOTE_PORT" "${REMOTE_USER}@${REMOTE_HOSTNAME}" << EOF
    if [ ! -r "${DEVICE}" ] || [ ! -w "${DEVICE}" ]; then
        echo "Sin permisos en ${DEVICE}, usando sudo..."
        sudo screen ${DEVICE} ${BAUDRATE} || sudo minicom -D ${DEVICE} -b ${BAUDRATE}
    else
        screen ${DEVICE} ${BAUDRATE} || minicom -D ${DEVICE} -b ${BAUDRATE}
    fi
EOF

