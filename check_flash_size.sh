#!/bin/bash
# Script para verificar el tamaño real del flash del ESP32

if [ $# -lt 2 ]; then
    echo "Uso: $0 [user@]hostname [port] [device]"
    echo "Ejemplo: $0 ogarcia@kid /dev/ttyUSB0"
    exit 1
fi

REMOTE_HOST="$1"
REMOTE_PORT="22"
DEVICE="/dev/ttyUSB0"

# Parsear argumentos
if [ $# -ge 2 ]; then
    if [[ "$2" =~ ^[0-9]+$ ]]; then
        REMOTE_PORT="$2"
        if [ $# -ge 3 ]; then
            DEVICE="$3"
        fi
    else
        DEVICE="$2"
    fi
fi

if [[ $REMOTE_HOST == *"@"* ]]; then
    REMOTE_USER=$(echo $REMOTE_HOST | cut -d'@' -f1)
    REMOTE_HOSTNAME=$(echo $REMOTE_HOST | cut -d'@' -f2)
else
    REMOTE_USER=$(whoami)
    REMOTE_HOSTNAME=$REMOTE_HOST
fi

echo "Verificando tamaño de flash en ${REMOTE_USER}@${REMOTE_HOSTNAME}:${REMOTE_PORT}..."
echo "Dispositivo: ${DEVICE}"
echo ""

ssh -p "$REMOTE_PORT" "${REMOTE_USER}@${REMOTE_HOSTNAME}" << EOF
    # Verificar si esptool está disponible
    if ! python3 -m esptool --help &> /dev/null; then
        echo "Instalando esptool..."
        pip3 install --user esptool 2>&1 | grep -v "already satisfied" || true
    fi
    
    # Detectar tamaño de flash
    echo "Detectando tamaño de flash..."
    sudo python3 -m esptool --port ${DEVICE} flash_id 2>&1 | grep -i "detected\|size" || echo "Error al detectar"
    
    # Leer información del chip
    echo ""
    echo "Información del chip:"
    sudo python3 -m esptool --port ${DEVICE} chip_id 2>&1 | head -5 || echo "Error al leer chip_id"
EOF

