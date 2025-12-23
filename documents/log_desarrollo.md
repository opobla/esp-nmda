# Log de Desarrollo

## 2024 - Módulo User LED para indicación de estado del sistema

### Cambios realizados

- **Archivo creado**: `main/user_led.c` - Implementación del módulo User LED
- **Archivo creado**: `main/include/user_led.h` - Header con API pública
- **Archivo modificado**: `main/Kconfig.projbuild` - Añadidas opciones de configuración
- **Archivo modificado**: `main/CMakeLists.txt` - Añadido user_led.c a la compilación
- **Archivo modificado**: `main/pulse_detection.c` - Eliminado código del LED USER
- **Archivo modificado**: `main/main.c` - Integrada inicialización temprana del módulo

### Detalles técnicos

Se ha creado un módulo dedicado para controlar el LED USER (GPIO 32 por defecto) que permite indicar diferentes condiciones del sistema mediante patrones de destellos configurables.

**Características principales:**
- Configurable mediante `sdkconfig` (GPIO, duraciones, pausas)
- Puede deshabilitarse completamente si no hay LED conectado
- Tarea FreeRTOS dedicada para ejecución no bloqueante
- Patrones predefinidos para diferentes estados del sistema
- API simple: `user_led_init()`, `user_led_set_condition()`, `user_led_off()`

**Patrones implementados:**
- `USER_LED_BOOTING`: 2 cortos + 2 largos (sistema arrancando)
- `USER_LED_WIFI_CONNECTING`: 2 cortos (WiFi conectando)
- `USER_LED_WIFI_ERROR`: 3 cortos + 1 largo (error WiFi)
- `USER_LED_NTP_CONNECTING`: 1 corto + 1 largo (NTP conectando)
- `USER_LED_NTP_ERROR`: 1 corto + 2 largos (error NTP)
- `USER_LED_DATA_ACQUISITION`: 1 corto (sistema adquiriendo datos, parpadeo continuo)
- `USER_LED_OFF`: LED apagado

**Configuración por defecto:**
- GPIO: 32
- Duración corto: 100ms
- Duración largo: 500ms
- Pausa entre destellos: 200ms
- Pausa entre ciclos: 1000ms (1 segundo)

**Inicialización:**
El módulo se inicializa al principio de `app_main()`, justo después de `init_nvs()`, para poder indicar el estado de arranque del sistema desde el inicio.

**Arquitectura:**
- Tarea FreeRTOS con prioridad media (3)
- Cola de mensajes para recibir comandos de cambio de condición
- Patrones definidos como arrays de duraciones (valores positivos = LED ON, negativos = LED OFF)
- Los patrones se repiten indefinidamente con pausa de 1 segundo entre ciclos

**Próximos pasos:**
- Integrar llamadas a `user_led_set_condition()` en módulos WiFi y SNTP para indicar cambios de estado
- Cambiar a `USER_LED_DATA_ACQUISITION` cuando el sistema esté completamente inicializado y adquiriendo datos

## 2024 - Eliminación de función no utilizada i2c_bus_write_read

### Cambios realizados

- **Archivo modificado**: `main/i2c_bus.c`
- **Archivo modificado**: `main/include/i2c_bus.h`
- **Función eliminada**: `i2c_bus_write_read()`

### Detalles técnicos

Se eliminó la función `i2c_bus_write_read()` porque:
1. No se utiliza en ningún lugar del código
2. Todos los usos requieren Repeated Start, por lo que se usa `i2c_bus_write_read_repeated_start()` en su lugar
3. La función eliminada tenía el problema de que podía enviar STOP entre write y read, lo cual rompe dispositivos como ADS112C04 que requieren Repeated Start

**Resultado**: Código más limpio y sin funciones no utilizadas. La única función disponible para write+read es `i2c_bus_write_read_repeated_start()`, que garantiza el uso correcto de Repeated Start Condition.

## 2024 - Configuración de navegación de código en Cursor con clangd

### Cambios realizados

- **Archivo creado**: `.vscode/c_cpp_properties.json` (para extensión C/C++ de Microsoft)
- **Archivo creado**: `.clangd` (configuración para clangd)
- **Enlace simbólico creado**: `compile_commands.json` -> `build/compile_commands.json`
- **Objetivo**: Hacer navegable el código de funciones de ESP-IDF como `i2c_master_transmit_receive` en Cursor

### Detalles técnicos

Se ha configurado IntelliSense para que Cursor pueda navegar a las definiciones de funciones de ESP-IDF:

1. **Archivo de configuración**: `.vscode/c_cpp_properties.json`
   - Configurado para usar `compile_commands.json` del directorio `build/`
   - Incluye paths de ESP-IDF y componentes del proyecto
   - Define macros y estándares de compilación necesarios

2. **Enlace simbólico**: Se creó un enlace desde la raíz del proyecto al `compile_commands.json` en `build/` para facilitar que Cursor lo encuentre automáticamente.

**Resultado**: Configurado clangd para navegación de código. clangd es más confiable que la extensión C/C++ de Microsoft para encontrar implementaciones de funciones.

**Configuración clangd**:
1. Archivo `.clangd` creado en la raíz del proyecto
2. Configurado para usar `compile_commands.json` del directorio `build/`
3. Incluye paths de ESP-IDF necesarios

**Uso**:
- Instalar extensión "clangd" en Cursor (llvm-vs-code-extensions.vscode-clangd)
- Desactivar extensión "C/C++" de Microsoft para evitar conflictos
- Recargar ventana de Cursor
- clangd indexará automáticamente usando `compile_commands.json`
- Usar `Cmd+Click` o `F12` para ir a definición
- Usar `Shift+F12` o `Cmd+Shift+F12` para ir a implementación

**Nota**: clangd indexa en segundo plano. Puede tardar unos minutos la primera vez. Verificar que el `compile_commands.json` esté actualizado ejecutando `idf.py build` si es necesario.

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

