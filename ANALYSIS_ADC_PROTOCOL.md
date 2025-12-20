# Análisis del Problema del ADC ADS112C04

## Fecha: 2025-12-20

## Problema
El ADC ADS112C04 no realiza conversiones. DRDY nunca se activa y todas las lecturas devuelven 0.

## Causa Raíz Identificada

Las funciones `hv_adc_write_register()` y `hv_adc_read_register()` **NO implementan correctamente el protocolo del ADS112C04**.

### Protocolo Correcto según Datasheet (sección 8.5.3)

**WREG (Write Register) - Command: 0100 nnxx**
- Bits 7-6: `01` (WREG command)
- Bits 5-2: `nn` = número de registro (0-3)
- Bits 1-0: `xx` = número de registros a escribir - 1 (generalmente `00` para 1 registro)

Secuencia I2C:
```
START → [Address+W] → [WREG command] → [Data byte] → STOP
```

Ejemplo para escribir CONFIG1 (registro 1) con valor 0x00:
```
START → [0x48+W] → [0x44] → [0x00] → STOP
                      ↑
                 0x40 | (1<<2) = 0x44
```

**RREG (Read Register) - Command: 0010 nnxx**
- Bits 7-6: `00` (RREG command bit 7-6)
- Bit 5: `1` (RREG command bit 5)
- Bits 4-2: `nn` = número de registro (0-3)  
- Bits 1-0: `xx` = número de registros a leer - 1

Secuencia I2C:
```
START → [Address+W] → [RREG command] → REPEATED START → [Address+R] → [Read data] → STOP
```

Ejemplo para leer CONFIG1 (registro 1):
```
START → [0x48+W] → [0x24] → REPEATED START → [0x48+R] → [Data] → STOP
                      ↑
                 0x20 | (1<<2) = 0x24
```

### Código Actual (INCORRECTO)

```c
static esp_err_t hv_adc_read_register(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_bus_read(HV_ADC_I2C_ADDR, &reg, 1, data, len, 1000);
}

static esp_err_t hv_adc_write_register(uint8_t reg, uint8_t data)
{
    return i2c_bus_write(HV_ADC_I2C_ADDR, &reg, 1, &data, 1, 1000);
}
```

**Problema:** Está enviando el número de registro directamente (0, 1, 2, 3) en lugar del comando WREG/RREG codificado.

### Código Correcto (A IMPLEMENTAR)

```c
// WREG command: 01nn nn00 where nnnn is the register number (shifted left by 2)
#define HV_ADC_CMD_WREG 0x40  // 0100 0000
#define HV_ADC_CMD_RREG 0x20  // 0010 0000

static esp_err_t hv_adc_read_register(uint8_t reg, uint8_t *data, size_t len)
{
    // Send RREG command: 0x20 | (reg << 2) | (len - 1)
    uint8_t rreg_cmd = HV_ADC_CMD_RREG | (reg << 2) | ((len - 1) & 0x03);
    
    ESP_LOGD(TAG, "Reading register %d: sending RREG command 0x%02X", reg, rreg_cmd);
    
    // Use write_read to send RREG command and then read data
    return i2c_bus_write_read(HV_ADC_I2C_ADDR, &rreg_cmd, 1, data, len, 1000);
}

static esp_err_t hv_adc_write_register(uint8_t reg, uint8_t data)
{
    // Send WREG command: 0x40 | (reg << 2)
    // For writing 1 register, the lower 2 bits are 00 (n-1 = 0)
    uint8_t wreg_cmd = HV_ADC_CMD_WREG | (reg << 2);
    uint8_t write_data[2] = {wreg_cmd, data};
    
    ESP_LOGD(TAG, "Writing register %d: sending WREG command 0x%02X with data 0x%02X", 
             reg, wreg_cmd, data);
    
    // Write both command and data in one transaction
    return i2c_bus_write(HV_ADC_I2C_ADDR, NULL, 0, write_data, 2, 1000);
}
```

## Por Qué CONFIG1 se Lee Como 0x11

1. Se intenta escribir `CONFIG1 = 0x00` enviando `[0x01, 0x00]` por I2C
2. El ADC interpreta `0x01` como un **comando inválido** (no es WREG ni RREG)
3. El registro no se actualiza
4. Al leer CONFIG1, se envía `[0x01]` que también es inválido
5. El ADC probablemente devuelve un valor por defecto o el último valor válido (0x11)

## Por Qué DRDY Nunca se Activa

1. Los registros de configuración nunca se escriben correctamente
2. El ADC no está configurado adecuadamente
3. Los comandos START/SYNC pueden no estar iniciando conversiones porque la configuración es incorrecta
4. Sin configuración válida, el ADC no puede realizar conversiones

## Solución

Implementar correctamente el protocolo WREG/RREG del ADS112C04 en las funciones `hv_adc_read_register` y `hv_adc_write_register`.

## Referencias

- ADS112C04 Datasheet, sección 8.5.3: "Serial Interface"
- Tabla 8-7: "Command Byte Structure"

