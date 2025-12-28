# SPL06-001 Sensor Support - Estado y Diagnóstico

## Resumen

Se ha implementado el soporte para el sensor de presión barométrica SPL06-001 con comunicación I2C. El sensor se detecta correctamente y se inicializa, pero hay un problema con las lecturas de temperatura que devuelve valores incorrectos (aproximadamente -2967°C en lugar de valores normales).

## Implementación Realizada

### Archivos Creados

1. **`main/include/spl06.h`**: Definiciones de registros, constantes y API pública
2. **`main/spl06.c`**: Driver del sensor SPL06-001 con:
   - Inicialización del sensor
   - Lectura de coeficientes de calibración
   - Lectura de presión y temperatura
   - Cálculo de valores compensados
3. **`main/spl06_monitor_task.c`**: Tarea FreeRTOS que lee periódicamente el sensor y envía datos a la cola de telemetría
4. **`main/include/i2c_bus.h`** y **`main/i2c_bus.c`**: Driver genérico I2C con:
   - Inicialización configurable (GPIO21/GPIO22 por defecto)
   - Funciones thread-safe de lectura/escritura
   - Función de escaneo I2C para detectar dispositivos

### Configuración

- **Dirección I2C**: 0x77 (configurado en `sdkconfig`, default cambiado de 0x76 a 0x77)
- **GPIO I2C**: GPIO21 (SDA), GPIO22 (SCL)
- **Velocidad I2C**: 100kHz (configurable)
- **Oversampling**: 8x para presión y temperatura
- **Rate**: 4 muestras por segundo

### Integración

- Añadido `TM_SPL06` a `datastructures.h` para telemetría
- Integrado en `main.c` con inicialización condicional
- Soporte MQTT añadido en `mss_sender.c` (temporalmente deshabilitado)
- Añadido a `CMakeLists.txt` para compilación

## Problema Identificado

### Síntomas

- El sensor se detecta correctamente (Product ID: 0x10)
- Los coeficientes de calibración se leen correctamente:
  - c0=203, c1=-253
  - c00=76934, c10=-52197
  - c01=-2582, c11=1244, c20=-9359, c21=111, c30=-1215
- La presión se lee correctamente (~805 hPa, valor razonable)
- **La temperatura devuelve valores incorrectos**: aproximadamente -2967°C

### Valores Raw Observados

- **Valor raw de temperatura**: 0x61085C (6359132 decimal)
- **Rango esperado**: -50000 a 50000
- **Valor observado**: 6359132 (más de 100 veces mayor que el máximo esperado)

### Análisis del Problema

El valor raw de temperatura tiene el byte más significativo (B2) en 0x61, lo cual es inusualmente alto. Para una temperatura ambiente normal, esperaríamos valores mucho más pequeños.

**Posibles causas**:

1. **Sensor no completamente inicializado**: Aunque los bits `PRS_RDY` y `TMP_RDY` están activos, el sensor podría necesitar más tiempo para estabilizarse
2. **Problema con la lectura de registros**: Los registros podrían estar siendo leídos en el orden incorrecto o con un problema de timing
3. **Configuración incorrecta**: La configuración del sensor (oversampling, rate) podría no ser la adecuada
4. **Problema de hardware**: Podría haber un problema físico con el sensor o las conexiones I2C

## Pruebas Realizadas

### 1. Verificación de Conexión I2C
- ✅ Escaneo I2C detecta dispositivo en dirección 0x77
- ✅ Product ID se lee correctamente (0x10)
- ✅ Coeficientes de calibración se leen correctamente

### 2. Verificación de Estado del Sensor
- ✅ Bits `PRS_RDY` y `TMP_RDY` se activan correctamente
- ✅ Función `spl06_is_ready()` funciona correctamente
- ⚠️ Sensor no está listo inmediatamente después de la configuración (requiere ~500ms)

### 3. Lectura de Registros
- ✅ Registros de temperatura se leen individualmente (0x03, 0x04, 0x05)
- ✅ Valores observados: B2=0x61, B1=0x08, B0=0x5C
- ❌ Valor raw combinado (0x61085C) es demasiado grande

### 4. Cálculo de Temperatura
- ✅ Fórmula implementada según datasheet: `T = c0 * 0.5 + c1 * Traw_sc`
- ✅ Factor de escala correcto: kT = 2^20 = 524288 (para PRC=8)
- ❌ Resultado del cálculo es incorrecto debido al valor raw erróneo

### 5. Lectura de Presión
- ✅ La presión se lee correctamente (~805 hPa)
- ✅ El cálculo de presión funciona correctamente
- Esto sugiere que el problema es específico de la temperatura

## Código de Diagnóstico Añadido

Se ha añadido logging extensivo para diagnosticar el problema:

1. **Logging de coeficientes de calibración** al cargarse
2. **Logging de valores raw** de temperatura (bytes individuales y valor combinado)
3. **Logging de estado del sensor** (bits de ready)
4. **Validación de rangos** para valores raw
5. **Logging de cálculos intermedios** (c0*0.5, c1*t_raw_sc)

## Próximos Pasos Sugeridos

### Para Retomar el Problema

1. **Verificar el datasheet del SPL06-001**:
   - Confirmar el orden correcto de lectura de registros
   - Verificar si hay algún bit de configuración adicional necesario
   - Revisar si el valor raw necesita alguna transformación especial

2. **Probar diferentes configuraciones**:
   - Diferentes valores de oversampling (PRC)
   - Diferentes rates de muestreo
   - Modo de una sola medición en lugar de continuo

3. **Verificar timing**:
   - Aumentar el tiempo de espera después de la configuración
   - Verificar si hay un delay necesario entre lectura de registros
   - Probar leer los registros en diferentes momentos

4. **Verificar hardware**:
   - Comprobar conexiones I2C (SDA, SCL, pull-ups)
   - Verificar alimentación del sensor (3.3V)
   - Comprobar si hay interferencias en las líneas I2C

5. **Comparar con implementaciones de referencia**:
   - Buscar ejemplos de código para SPL06-001
   - Verificar si hay diferencias en la implementación

## Referencias

- **Sensor**: SPL06-001 (Goertek)
- **Comunicación**: I2C (dirección 0x77)
- **Registros de temperatura**: 0x03 (TMP_B2), 0x04 (TMP_B1), 0x05 (TMP_B0)
- **Product ID**: 0x10
- **Fórmula de temperatura**: `T = c0 * 0.5 + c1 * Traw_sc`
- **Factor de escala (kT)**: 2^20 = 524288 (para PRC=8)

## Estado Actual

- ✅ Sensor detectado e inicializado correctamente
- ✅ Presión funcionando correctamente
- ❌ Temperatura con valores incorrectos (pendiente de resolver)
- ✅ Infraestructura I2C funcionando correctamente
- ✅ Integración en el sistema completada

## Notas Adicionales

- El sensor está configurado en modo continuo (MEAS_CFG = 0x07)
- Se descarta la primera lectura después de la inicialización para estabilización
- La tarea de monitorización se ejecuta en Core 1
- El soporte MQTT está implementado pero temporalmente deshabilitado

