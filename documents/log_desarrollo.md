# Log de Desarrollo

## 2024 - Eliminación de mensajes de error en escaneo I2C

### Cambios realizados

- **Archivo modificado**: `main/i2c_bus.c`
- **Función**: `i2c_bus_scan()`
- **Cambio**: Eliminados los mensajes de advertencia (`ESP_LOGW`) que se mostraban cuando no se detectaban dispositivos I2C en el bus durante la secuencia de arranque.
- **Resultado**: El log queda más compacto y solo muestra información cuando se encuentran dispositivos. Si no hay dispositivos, la función completa el escaneo silenciosamente.

### Detalles técnicos

Se eliminaron las siguientes líneas de la función `i2c_bus_scan()`:
- `ESP_LOGW(TAG, "No I2C devices found on bus");`
- `ESP_LOGW(TAG, "Check connections: SDA=GPIO%d, SCL=GPIO%d", ...);`

Ahora solo se muestra un mensaje informativo cuando se encuentran dispositivos:
- `ESP_LOGI(TAG, "Found %d device(s) on I2C bus", found_count);`

**Nota**: Los mensajes de error del driver I2C (`i2c.master`) siguen apareciendo durante el escaneo, ya que es el comportamiento normal del driver al intentar comunicarse con direcciones que no tienen dispositivos conectados.

