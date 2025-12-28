# Análisis del Problema del ADC ADS112C04

## Fecha: 2025-12-20

## Problema Inicial
El ADC ADS112C04 no realiza conversiones. DRDY nunca se activa y todas las lecturas devuelven 0.

---

## Causa Raíz #1: Protocolo WREG/RREG Incorrecto ✅ RESUELTO

### Problema
Las funciones `hv_adc_write_register()` y `hv_adc_read_register()` **NO implementaban correctamente el protocolo del ADS112C04**.

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

### Código Anterior (INCORRECTO)

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

**Problema:** Estaba enviando el número de registro directamente (0, 1, 2, 3) en lugar del comando WREG/RREG codificado.

### Código Correcto (IMPLEMENTADO)

```c
// Command definitions
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

### Resultado de la Corrección #1
✅ Los registros ahora se escriben y leen correctamente
✅ CONFIG1 se lee correctamente como 0x00 (antes era 0x11)
❌ **PERO**: DRDY todavía no se activa, conversiones devuelven 0

---

## Causa Raíz #2: CONFIG2 DCNT Bit No Habilitado ✅ RESUELTO

### Problema
Aunque los registros ahora se escriben correctamente, **DRDY nunca se activaba** porque CONFIG2 estaba mal configurado.

### Análisis
Según el datasheet ADS112C04 sección 8.6.2.3 "Data Ready by Reading":

**CONFIG2 Register (0x02)**
- **Bit 7 (DRDY)**: Data Ready flag (read-only)
  - `1` = Nueva conversión completada, datos listos
  - `0` = No hay datos nuevos
  - **IMPORTANTE**: Este bit solo es accesible si DCNT=1

- **Bit 6 (DCNT)**: Data Counter Enable
  - `0` = DRDY solo disponible en pin físico DRDY
  - **`1` = DRDY disponible en CONFIG2 bit 7 (lectura por I2C)** ← ¡Necesario para polling!

- Bits 5-4: CRC mode (00 = disabled)
- Bit 3: BCS = Burn-out current sources
- Bits 2-0: IDAC current setting

### Código Anterior (INCORRECTO)

```c
// Configure CONFIG2: Default settings (IDAC off, power switch default, no FIR)
uint8_t config2 = 0x00;  // ❌ DCNT=0, DRDY no disponible en I2C!
ret = hv_adc_write_register(HV_ADC_REG_CONFIG2, config2);
```

**Problema:** Con CONFIG2=0x00, el bit DCNT está en 0, por lo que:
1. DRDY solo aparece en el pin físico DRDY (no conectado)
2. El polling por I2C de CONFIG2 bit 7 siempre devuelve 0
3. El software nunca detecta cuando hay datos listos

### Código Correcto (IMPLEMENTADO)

```c
// Configure CONFIG2: Enable data counter (DCNT=1, bit 6) to make DRDY available in CONFIG2 bit 7
// According to datasheet section 8.6.2.3:
// - Bit 7 (DRDY): Data ready flag (read-only, set by ADC when conversion complete)
// - Bit 6 (DCNT): Data counter enable (1 = enable, makes DRDY available in bit 7)
// - Bits 5-4: CRC mode (00 = disabled)
// - Bit 3: BCS = 0 (burn-out current sources off)
// - Bits 2-0: IDAC current setting (000 = off)
uint8_t config2 = HV_ADC_CONFIG2_DCNT;  // 0x40 - Enable data counter to get DRDY in CONFIG2
ret = hv_adc_write_register(HV_ADC_REG_CONFIG2, config2);
```

Con **CONFIG2 = 0x40** (DCNT=1):
- ✅ DRDY flag se actualiza en CONFIG2 bit 7 tras cada conversión
- ✅ El polling por I2C de `hv_adc_is_ready()` funciona correctamente
- ✅ El software puede detectar cuando hay datos disponibles

### Actualización de Definiciones

También se corrigieron las definiciones de CONFIG2/CONFIG3 en `hv_adc.h` para coincidir con el datasheet:

```c
// Configuration register 2 bits
#define HV_ADC_CONFIG2_DRDY       (1 << 7)  // Data ready flag (read-only)
#define HV_ADC_CONFIG2_DCNT       (1 << 6)  // Data counter enable ← ¡Clave!
#define HV_ADC_CONFIG2_CRC_MASK   0x30
#define HV_ADC_CONFIG2_CRC_SHIFT  4
#define HV_ADC_CONFIG2_BCS        (1 << 3)  // Burn-out current sources
#define HV_ADC_CONFIG2_IDAC_MASK  0x07
#define HV_ADC_CONFIG2_IDAC_SHIFT 0
```

---

## Resultado Final Esperado

Con ambas correcciones aplicadas:
1. ✅ Registros se escriben y leen con protocolo WREG/RREG correcto
2. ✅ CONFIG2 tiene DCNT=1, habilitando DRDY en bit 7
3. ✅ Las conversiones deberían completarse correctamente
4. ✅ El bit DRDY debería activarse tras cada conversión
5. ✅ Las lecturas deberían devolver valores reales en lugar de 0

---

## Referencias

- **ADS112C04 Datasheet**:
  - Sección 8.5.3: "Serial Interface" (protocolo WREG/RREG)
  - Sección 8.5.3.3: "RREG Command"
  - Sección 8.5.3.4: "WREG Command"
  - Sección 8.6.2.3: "Data Ready by Reading" (CONFIG2 DCNT bit)
  - Tabla 8-7: "Command Byte Structure"
  - Tabla 8-8: "Register Map"

---

## Commits Relacionados

1. `b3ee325` - fix: Implement correct WREG/RREG protocol for ADS112C04
2. `[nuevo]` - fix: Enable DRDY flag in CONFIG2 register
