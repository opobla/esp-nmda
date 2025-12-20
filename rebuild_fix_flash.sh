#!/bin/bash
# Script para recompilar limpiamente con la configuración correcta de flash

set -e

echo "=========================================="
echo "Recompilación Limpia para Flash Size"
echo "=========================================="
echo ""

# Verificar que estamos en el directorio correcto
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: Este script debe ejecutarse desde el directorio raíz del proyecto"
    exit 1
fi

# Verificar configuración
echo "1. Verificando configuración..."
if grep -q "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y" sdkconfig.defaults; then
    echo "   ✅ Configurado para 8MB"
else
    echo "   ⚠️  No se encontró CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y"
    echo "   Verificando otras configuraciones..."
    grep "CONFIG_ESPTOOLPY_FLASHSIZE" sdkconfig.defaults || echo "   No se encontró configuración de flash size"
fi

echo ""
echo "2. Limpiando build anterior y configuración..."
idf.py fullclean
# Eliminar sdkconfig para forzar regeneración desde sdkconfig.defaults
rm -f sdkconfig sdkconfig.old
echo "   ✅ sdkconfig eliminado (se regenerará desde sdkconfig.defaults)"

echo ""
echo "3. Recompilando proyecto..."
idf.py build

echo ""
echo "4. Verificando header del bootloader..."
if [ -f "build/bootloader/bootloader.bin" ]; then
    python3 << 'PYEOF'
with open('build/bootloader/bootloader.bin', 'rb') as f:
    header = f.read(16)
    byte3 = header[3]
    flash_size_code = (byte3 >> 3) & 0x1F
    flash_mode = byte3 & 0x07
    sizes = {0x4: '4MB', 0x5: '8MB', 0x6: '16MB'}
    modes = {0: 'QIO', 1: 'QOUT', 2: 'DIO', 3: 'DOUT'}
    print(f"   Byte 3 del header: 0x{byte3:02x}")
    print(f"   Flash size code: 0x{flash_size_code:x}")
    print(f"   Flash mode: {flash_mode} ({modes.get(flash_mode, 'Unknown')})")
    size_str = sizes.get(flash_size_code, f"Unknown (0x{flash_size_code:x})")
    print(f"   Flash size configurado: {size_str}")
    if flash_size_code == 0x5 and flash_mode == 2:
        print("   ✅ Bootloader compilado correctamente para 8MB DIO")
    else:
        print(f"   ⚠️  Bootloader compilado para {size_str} {modes.get(flash_mode, 'Unknown')}, esperado 8MB DIO")
        print("   🔧 Corrigiendo header del bootloader...")
PYEOF
    
    # Corregir el header si es necesario
    python3 fix_bootloader_header.py build/bootloader/bootloader.bin
    
    # Verificar después de la corrección
    echo ""
    echo "   Verificando después de la corrección:"
    python3 << 'PYEOF'
with open('build/bootloader/bootloader.bin', 'rb') as f:
    header = f.read(16)
    byte3 = header[3]
    flash_size_code = (byte3 >> 3) & 0x1F
    flash_mode = byte3 & 0x07
    sizes = {0x4: '4MB', 0x5: '8MB', 0x6: '16MB'}
    modes = {0: 'QIO', 1: 'QOUT', 2: 'DIO', 3: 'DOUT'}
    if flash_size_code == 0x5 and flash_mode == 2:
        print("   ✅ Header corregido: 8MB DIO")
    else:
        print(f"   ⚠️  Header aún incorrecto: {sizes.get(flash_size_code, 'Unknown')} {modes.get(flash_mode, 'Unknown')}")
PYEOF
else
    echo "   ❌ No se encontró bootloader.bin"
fi

echo ""
echo "=========================================="
echo "Recompilación completada"
echo "=========================================="
echo ""
echo "Siguiente paso:"
echo "  ./deploy_remote.sh ogarcia@kid /dev/ttyUSB0"

