#!/usr/bin/env python3
"""
Script para corregir el header del bootloader ESP32
Fuerza el tamaño de flash a 8MB y modo DIO
"""

import sys
import os

def detect_flash_size_from_config():
    """Detecta el tamaño de flash desde la configuración"""
    # Intentar leer desde sdkconfig
    if os.path.exists('sdkconfig'):
        with open('sdkconfig', 'r') as f:
            content = f.read()
            if 'CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y' in content:
                return 8
            elif 'CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y' in content:
                return 4
    
    # Intentar leer desde sdkconfig.defaults
    if os.path.exists('sdkconfig.defaults'):
        with open('sdkconfig.defaults', 'r') as f:
            content = f.read()
            if 'CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y' in content:
                return 8
            elif 'CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y' in content:
                return 4
    
    # Por defecto, asumir 4MB
    return 4

def fix_bootloader_header(bootloader_path, flash_size_mb=None):
    """Corrige el byte 3 del header del bootloader para el tamaño especificado"""
    
    if not os.path.exists(bootloader_path):
        print(f"Error: {bootloader_path} no existe")
        return False
    
    # Detectar tamaño de flash si no se especifica
    if flash_size_mb is None:
        flash_size_mb = detect_flash_size_from_config()
    
    with open(bootloader_path, 'rb') as f:
        data = bytearray(f.read())
    
    if len(data) < 16:
        print(f"Error: {bootloader_path} es demasiado pequeño")
        return False
    
    # Leer byte 3 actual
    byte3_old = data[3]
    flash_size_code_old = (byte3_old >> 3) & 0x1F
    flash_mode_old = byte3_old & 0x07
    
    print(f"Header actual:")
    print(f"  Byte 3: 0x{byte3_old:02x}")
    print(f"  Flash size code: 0x{flash_size_code_old:x}")
    print(f"  Flash mode: {flash_mode_old}")
    
    # Configurar según el tamaño detectado
    # Flash size codes: 4=4MB, 5=8MB, 6=16MB
    # Flash mode: 2 (DIO)
    flash_size_codes = {4: 4, 8: 5, 16: 6}
    flash_size_code_new = flash_size_codes.get(flash_size_mb, 4)
    flash_mode_new = 2  # DIO
    byte3_new = (flash_size_code_new << 3) | flash_mode_new
    
    print(f"\nCorrigiendo a:")
    print(f"  Byte 3: 0x{byte3_new:02x}")
    print(f"  Flash size code: 0x{flash_size_code_new:x} ({flash_size_mb}MB)")
    print(f"  Flash mode: {flash_mode_new} (DIO)")
    
    # Modificar el byte 3
    data[3] = byte3_new
    
    # Crear backup
    backup_path = bootloader_path + '.backup'
    with open(backup_path, 'wb') as f:
        f.write(bytes(data))
        # Restaurar byte original para el backup
        data[3] = byte3_old
    
    # Escribir archivo corregido
    with open(bootloader_path, 'wb') as f:
        f.write(data)
    
    print(f"\n✅ Header corregido")
    print(f"   Backup guardado en: {backup_path}")
    
    return True

if __name__ == '__main__':
    bootloader_path = 'build/bootloader/bootloader.bin'
    flash_size = None
    
    # Parsear argumentos
    for i, arg in enumerate(sys.argv[1:], 1):
        if arg == '--size' and i + 1 < len(sys.argv):
            flash_size = int(sys.argv[i + 1])
        elif not arg.startswith('--'):
            bootloader_path = arg
    
    if fix_bootloader_header(bootloader_path, flash_size):
        print("\n✅ Bootloader corregido exitosamente")
        sys.exit(0)
    else:
        print("\n❌ Error al corregir bootloader")
        sys.exit(1)

