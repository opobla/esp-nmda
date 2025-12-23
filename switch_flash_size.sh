#!/bin/bash
# Script para cambiar entre configuración de 4MB y 8MB

set -e

if [ $# -lt 1 ]; then
    echo "Uso: $0 [4mb|8mb]"
    echo ""
    echo "Este script cambia la configuración entre 4MB y 8MB de flash"
    echo ""
    echo "Opciones:"
    echo "  4mb  - Configurar para ESP32 con 4MB de flash"
    echo "  8mb  - Configurar para ESP32 con 8MB de flash"
    exit 1
fi

SIZE=$1

if [ "$SIZE" != "4mb" ] && [ "$SIZE" != "8mb" ]; then
    echo "Error: Tamaño debe ser '4mb' o '8mb'"
    exit 1
fi

echo "=========================================="
echo "Cambiando configuración a ${SIZE^^}"
echo "=========================================="
echo ""

# Verificar que los archivos de backup existen
if [ ! -f "sdkconfig.defaults.8mb" ]; then
    echo "⚠️  Advertencia: sdkconfig.defaults.8mb no encontrado"
fi

if [ ! -f "partitions/partitions_8mb.csv" ]; then
    echo "⚠️  Advertencia: partitions/partitions_8mb.csv no encontrado"
fi

if [ "$SIZE" == "4mb" ]; then
    echo "Configurando para 4MB..."
    
    # Actualizar sdkconfig.defaults
    if [ -f "sdkconfig.defaults.8mb" ]; then
        # Crear backup del actual si no es el de 8MB
        if ! grep -q "CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y" sdkconfig.defaults 2>/dev/null; then
            cp sdkconfig.defaults sdkconfig.defaults.backup
            echo "  ✅ Backup del sdkconfig.defaults actual guardado"
        fi
    fi
    
    # Modificar sdkconfig.defaults para 4MB
    sed -i.bak \
        -e 's/CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y/# CONFIG_ESPTOOLPY_FLASHSIZE_8MB is not set/' \
        -e 's/# CONFIG_ESPTOOLPY_FLASHSIZE_4MB is not set/CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y/' \
        -e 's/CONFIG_ESPTOOLPY_FLASHSIZE="8MB"/CONFIG_ESPTOOLPY_FLASHSIZE="4MB"/' \
        -e 's|partitions/partitions.csv|partitions/partitions_4mb.csv|' \
        -e 's|partitions/partitions_8mb.csv|partitions/partitions_4mb.csv|' \
        sdkconfig.defaults
    
    rm -f sdkconfig.defaults.bak
    
    echo "  ✅ sdkconfig.defaults actualizado para 4MB"
    echo "  ✅ Partition table configurada para partitions_4mb.csv"
    
elif [ "$SIZE" == "8mb" ]; then
    echo "Configurando para 8MB..."
    
    # Restaurar desde backup
    if [ -f "sdkconfig.defaults.8mb" ]; then
        cp sdkconfig.defaults.8mb sdkconfig.defaults
        echo "  ✅ sdkconfig.defaults restaurado desde backup 8MB"
    else
        # Modificar manualmente
        sed -i.bak \
            -e 's/CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y/# CONFIG_ESPTOOLPY_FLASHSIZE_4MB is not set/' \
            -e 's/# CONFIG_ESPTOOLPY_FLASHSIZE_8MB is not set/CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y/' \
            -e 's/CONFIG_ESPTOOLPY_FLASHSIZE="4MB"/CONFIG_ESPTOOLPY_FLASHSIZE="8MB"/' \
            -e 's|partitions/partitions_4mb.csv|partitions/partitions.csv|' \
            sdkconfig.defaults
        
        rm -f sdkconfig.defaults.bak
        echo "  ✅ sdkconfig.defaults actualizado para 8MB"
    fi
    
    # Restaurar partition table
    if [ -f "partitions/partitions_8mb.csv" ]; then
        cp partitions/partitions_8mb.csv partitions/partitions.csv
        echo "  ✅ Partition table restaurada desde partitions_8mb.csv"
    else
        echo "  ⚠️  partitions/partitions_8mb.csv no encontrado, usando partitions.csv actual"
    fi
fi

echo ""
echo "=========================================="
echo "Configuración cambiada a ${SIZE^^}"
echo "=========================================="
echo ""
echo "Siguiente paso:"
echo "  1. Eliminar sdkconfig: rm -f sdkconfig sdkconfig.old"
echo "  2. Recompilar: idf.py fullclean && idf.py build"
echo ""








