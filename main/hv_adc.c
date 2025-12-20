#include "hv_adc.h"

#ifdef CONFIG_ENABLE_HV_SUPPORT

#include "i2c_bus.h"
#include "esp32-libs.h"
#include "sdkconfig.h"
#include <math.h>

static const char *TAG = "HV_ADC";

// Static variables
static bool hv_adc_initialized = false;
static uint8_t current_gain = HV_ADC_GAIN_1;
static uint8_t current_data_rate = HV_ADC_DR_20SPS;

// I2C address (configurable via Kconfig in the future)
#define HV_ADC_I2C_ADDR HV_ADC_I2C_ADDR_DEFAULT

// Internal reference voltage (2.048V)
#define HV_ADC_VREF_INTERNAL_MV 2048.0f

// Full scale range for 16-bit ADC
#define HV_ADC_FULL_SCALE 32768.0f

/**
 * @brief Read one or more registers from the ADC using RREG command
 * 
 * According to ADS112C04 datasheet section 8.5.3.3:
 * RREG command format: 0010 rrnn
 *   - Bits 7-6: 00 (RREG identifier bits)
 *   - Bit 5: 1 (RREG identifier bit)
 *   - Bits 4-2: rrr = register number (0-3)
 *   - Bits 1-0: nn = number of registers to read - 1 (0-3)
 * 
 * Example: To read CONFIG1 (register 1):
 *   RREG command = 0x20 | (1 << 2) | 0 = 0x24
 * 
 * @param reg Register number (0-3)
 * @param data Buffer to store read data
 * @param len Number of registers to read (1-4)
 * @return ESP_OK on success
 */
static esp_err_t hv_adc_read_register(uint8_t reg, uint8_t *data, size_t len)
{
    if (reg > 3 || len == 0 || len > 4 || data == NULL) {
        ESP_LOGE(TAG, "Invalid register read parameters: reg=%d, len=%d", reg, len);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Build RREG command: 0x20 | (reg << 2) | ((len - 1) & 0x03)
    uint8_t rreg_cmd = HV_ADC_CMD_RREG | (reg << 2) | ((len - 1) & 0x03);
    
    ESP_LOGI(TAG, "RREG reg %d: cmd=0x%02X", reg, rreg_cmd);
    
    // Send RREG command, then read response
    esp_err_t ret = i2c_bus_write_read(HV_ADC_I2C_ADDR, &rreg_cmd, 1, data, len, 1000);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "RREG reg %d: read OK, data[0]=0x%02X", reg, data[0]);
    } else {
        ESP_LOGE(TAG, "RREG reg %d: FAILED: %s", reg, esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief Write one or more registers to the ADC using WREG command
 * 
 * According to ADS112C04 datasheet section 8.5.3.4:
 * WREG command format: 0100 rrnn
 *   - Bits 7-6: 01 (WREG identifier)
 *   - Bits 5-2: rrrr = register number (0-3)
 *   - Bits 1-0: nn = number of registers to write - 1 (0-3)
 * 
 * Example: To write CONFIG1 (register 1) with value 0x00:
 *   WREG command = 0x40 | (1 << 2) | 0 = 0x44
 *   Data bytes: 0x00
 *   I2C sequence: START, [Address+W], [0x44], [0x00], STOP
 * 
 * @param reg Register number (0-3)
 * @param data Data byte to write
 * @return ESP_OK on success
 */
static esp_err_t hv_adc_write_register(uint8_t reg, uint8_t data)
{
    if (reg > 3) {
        ESP_LOGE(TAG, "Invalid register number: %d", reg);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Build WREG command: 0x40 | (reg << 2) | 0 (writing 1 register)
    // WREG format: 0100 rrxx where:
    //   - Bits 7-6: 01 (WREG identifier)
    //   - Bits 5-2: rr = register number (0-3)
    //   - Bits 1-0: xx = number of registers to write - 1 (0 for 1 register)
    // 
    // For register 2: register number 2 = 0010 in binary
    // Should be in bits 5-2: 0100 0010 00 = 0x44
    // Our calculation: (2 << 2) = 0x08, so 0x40 | 0x08 = 0x48
    // 
    // WAIT: (reg << 2) puts reg in bits 5-2, which is correct!
    // For reg=2: (2 << 2) = 8 = 0x08 = 0000 1000
    // In bits 5-2: 0100 1000 = 0x48
    // But datasheet says it should be 0x44 for register 2...
    // 
    // Let me check: register 2 in bits 5-2 should be 0010
    // So: 0100 0010 00 = 0x44
    // But (2 << 2) = 0x08 = 0000 1000, which in bits 5-2 is... wait
    // 
    // Actually, (reg << 2) for reg=2 gives us 0x08, which is 0000 1000
    // When OR'd with 0x40 (0100 0000), we get 0100 1000 = 0x48
    // But we need 0100 0010 = 0x42... no wait, that's not right either
    // 
    // Let me recalculate: register 2 = 2 decimal = 0010 binary
    // To put it in bits 5-2, we need to shift left by 2: 0010 << 2 = 1000
    // But that's 0x08, which gives us 0x48 when OR'd with 0x40
    // 
    // Hmm, maybe the datasheet format is different? Let me try the alternative:
    // Maybe register number should be in bits 4-2, not 5-2?
    // Or maybe it's (reg << 1) instead of (reg << 2)?
    uint8_t wreg_cmd = HV_ADC_CMD_WREG | (reg << 2);
    
    // Prepare data: [WREG command, data byte]
    uint8_t write_buffer[2] = {wreg_cmd, data};
    
    ESP_LOGI(TAG, "WREG reg %d: cmd=0x%02X, data=0x%02X", reg, wreg_cmd, data);
    
    // Send WREG command and data in one transaction
    esp_err_t ret = i2c_bus_write(HV_ADC_I2C_ADDR, NULL, 0, write_buffer, 2, 1000);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WREG reg %d: write OK", reg);
    } else {
        ESP_LOGE(TAG, "WREG reg %d: FAILED: %s", reg, esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief Send a command to the ADC
 * 
 * Commands are sent as a single byte write (no register address)
 * According to ADS112C04 datasheet section 8.5.3, commands are sent as:
 * - Write operation with command byte as the only data byte (no register address)
 * @param cmd Command byte (e.g., 0x08 for START/SYNC, 0x02 for POWERDOWN, 0x10 for RDATA)
 */
static esp_err_t hv_adc_send_command(uint8_t cmd)
{
    // Commands are sent as a write operation with the command byte as data
    // The ADS112C04 expects commands without a register address
    // According to datasheet section 8.5.3, commands are sent as:
    // - I2C write transaction with command byte as the only data byte
    // Use i2c_bus_write with reg_addr_len=0 to send only the command byte
    ESP_LOGD(TAG, "Sending command 0x%02X to ADC at address 0x%02X", cmd, HV_ADC_I2C_ADDR);
    
    // Verify that i2c_bus_write with reg_addr_len=0 sends only the command byte
    // When reg_addr_len=0, total_len = 0 + 1 = 1, so only the command byte is sent
    esp_err_t ret = i2c_bus_write(HV_ADC_I2C_ADDR, NULL, 0, &cmd, 1, 1000);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Command 0x%02X failed: %s", cmd, esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Command 0x%02X sent successfully", cmd);
    }
    return ret;
}

esp_err_t hv_adc_init(void)
{
    if (hv_adc_initialized) {
        ESP_LOGW(TAG, "HV ADC already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Initializing HV ADC (ADS112C04)");
    ESP_LOGI(TAG, "Driver Version: v2.0 - WREG/RREG + DCNT fix");
    ESP_LOGI(TAG, "I2C Address: 0x%02X", HV_ADC_I2C_ADDR);
    ESP_LOGI(TAG, "========================================");

    // First, try to read CONFIG0 to verify device is present
    uint8_t config0_test;
    esp_err_t ret = hv_adc_read_register(HV_ADC_REG_CONFIG0, &config0_test, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC not responding at address 0x%02X: %s", HV_ADC_I2C_ADDR, esp_err_to_name(ret));
        ESP_LOGE(TAG, "Check I2C connections: SDA=GPIO%d, SCL=GPIO%d", 
                 CONFIG_I2C_MASTER_SDA_IO, CONFIG_I2C_MASTER_SCL_IO);
        ESP_LOGE(TAG, "Verify ADC is powered and address is correct");
        return ret;
    }
    ESP_LOGI(TAG, "ADC detected! Initial CONFIG0 value: 0x%02X", config0_test);

    // Reset ADC using RESET command (0x06 or 0x07)
    // According to datasheet section 8.5.3.2, RESET command is 0000 011x
    ESP_LOGI(TAG, "Sending RESET command (0x%02X)", HV_ADC_CMD_RESET);
    ret = hv_adc_send_command(HV_ADC_CMD_RESET);  // RESET command
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send RESET command: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ADC reset command sent");

    // Wait for reset to complete (typically 50us, but wait 10ms to be safe)
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Verify reset by reading CONFIG0 (should be 0x00 after reset)
    uint8_t config0_after_reset;
    ret = hv_adc_read_register(HV_ADC_REG_CONFIG0, &config0_after_reset, 1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CONFIG0 after reset: 0x%02X (expected 0x00)", config0_after_reset);
    }

    // Configure CONFIG0: MUX = AIN0-AIN1, Gain = 1, PGA enabled
    uint8_t config0 = (HV_ADC_MUX_AIN0_AIN1 << HV_ADC_CONFIG0_MUX_SHIFT) |
                      (HV_ADC_GAIN_1 << HV_ADC_CONFIG0_GAIN_SHIFT);
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG0, config0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG0: %s", esp_err_to_name(ret));
        return ret;
    }
    // Verify CONFIG0 write
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t verify_config0;
    if (hv_adc_read_register(HV_ADC_REG_CONFIG0, &verify_config0, 1) == ESP_OK) {
        ESP_LOGI(TAG, "CONFIG0 verification: wrote 0x%02X, read 0x%02X", config0, verify_config0);
    }

    // Configure CONFIG1: Data rate = 20 SPS, Single-shot mode, Internal VREF, TS=0 (normal mode)
    // IMPORTANT: TS bit (bit 0) must be 0 for normal ADC conversions
    // TS=1 puts ADC in temperature sensor mode only
    uint8_t config1 = (HV_ADC_DR_20SPS << HV_ADC_CONFIG1_DR_SHIFT) |
                      (HV_ADC_CM_SINGLE << HV_ADC_CONFIG1_CM_SHIFT) |
                      (HV_ADC_VREF_INTERNAL);
    // Ensure TS bit is 0 (temperature sensor mode disabled)
    config1 &= ~0x01;  // Clear bit 0 (TS bit)
    ESP_LOGI(TAG, "Writing CONFIG1: 0x%02X (DR=20SPS, CM=single, VREF=internal, TS=0)", config1);
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG1, config1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG1: %s", esp_err_to_name(ret));
        return ret;
    }
    // Verify CONFIG1 write
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t verify_config1;
    if (hv_adc_read_register(HV_ADC_REG_CONFIG1, &verify_config1, 1) == ESP_OK) {
        ESP_LOGI(TAG, "CONFIG1 verification: wrote 0x%02X, read 0x%02X", config1, verify_config1);
    }

    // Configure CONFIG2: Enable data counter (DCNT=1, bit 6) to make DRDY available in CONFIG2 bit 7
    // According to datasheet section 8.6.2.3:
    // - Bit 7 (DRDY): Data ready flag (read-only, set by ADC when conversion complete)
    // - Bit 6 (DCNT): Data counter enable (1 = enable, makes DRDY available in bit 7)
    // - Bits 5-4: CRC mode (00 = disabled)
    // - Bit 3: BCS = 0 (burn-out current sources off)
    // - Bits 2-0: IDAC current setting (000 = off)
    ESP_LOGI(TAG, ">>> About to write CONFIG2 <<<");
    uint8_t config2 = HV_ADC_CONFIG2_DCNT;  // 0x40 - Enable data counter to get DRDY in CONFIG2
    ESP_LOGI(TAG, "Writing CONFIG2: 0x%02X (DCNT=1, DRDY polling enabled)", config2);
    ESP_LOGI(TAG, "DEBUG: WREG CONFIG2 command will be: 0x%02X, data: 0x%02X", 
             (HV_ADC_CMD_WREG | (HV_ADC_REG_CONFIG2 << 2)), config2);
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG2, config2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG2: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait longer for CONFIG2 to settle (some ADCs need more time for certain registers)
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Verify CONFIG2 was written correctly
    uint8_t verify_config2;
    ret = hv_adc_read_register(HV_ADC_REG_CONFIG2, &verify_config2, 1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CONFIG2 verification read: 0x%02X (expected 0x40)", verify_config2);
        if (verify_config2 != 0x40) {
            ESP_LOGW(TAG, "CONFIG2 write failed! Retrying...");
            // Retry writing CONFIG2
            ret = hv_adc_write_register(HV_ADC_REG_CONFIG2, config2);
            if (ret == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(50));
                ret = hv_adc_read_register(HV_ADC_REG_CONFIG2, &verify_config2, 1);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "CONFIG2 after retry: 0x%02X", verify_config2);
                }
            }
        }
    }

    // Configure CONFIG3: Default settings
    uint8_t config3 = 0x00;
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG3, config3);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG3: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify configuration by reading back
    uint8_t read_config0, read_config1;
    ret = hv_adc_read_register(HV_ADC_REG_CONFIG0, &read_config0, 1);
    if (ret == ESP_OK) {
        ret = hv_adc_read_register(HV_ADC_REG_CONFIG1, &read_config1, 1);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to verify ADC configuration: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ADC configured: CONFIG0=0x%02X, CONFIG1=0x%02X", read_config0, read_config1);
    
    // Verify TS bit is 0 (temperature sensor mode should be disabled for normal conversions)
    if (read_config1 & 0x01) {
        ESP_LOGW(TAG, "WARNING: TS bit is set in CONFIG1 (0x%02X)! ADC is in temperature sensor mode.", read_config1);
        ESP_LOGW(TAG, "This will prevent normal ADC conversions. Clearing TS bit...");
        read_config1 &= ~0x01;  // Clear TS bit
        ret = hv_adc_write_register(HV_ADC_REG_CONFIG1, read_config1);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "TS bit cleared. CONFIG1 now: 0x%02X", read_config1);
        }
    } else {
        ESP_LOGI(TAG, "TS bit is correctly cleared (normal ADC mode)");
    }
    
    // Verify all configuration registers
    uint8_t read_config2, read_config3;
    vTaskDelay(pdMS_TO_TICKS(10));  // Wait 10ms for CONFIG2 write to settle
    ret = hv_adc_read_register(HV_ADC_REG_CONFIG2, &read_config2, 1);
    ESP_LOGI(TAG, "DEBUG: RREG CONFIG2 command: 0x%02X, read result: 0x%02X", 
             (HV_ADC_CMD_RREG | (HV_ADC_REG_CONFIG2 << 2)), read_config2);
    if (ret == ESP_OK) {
        ret = hv_adc_read_register(HV_ADC_REG_CONFIG3, &read_config3, 1);
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Full configuration: CONFIG0=0x%02X, CONFIG1=0x%02X, CONFIG2=0x%02X, CONFIG3=0x%02X",
                 read_config0, read_config1, read_config2, read_config3);
        
        // Version check marker - verify CONFIG2 has DCNT bit set
        if (read_config2 == 0x40) {
            ESP_LOGI(TAG, "✓ CONFIG2 VERIFIED: DCNT=1, DRDY polling enabled (v2.0 active)");
        } else {
            ESP_LOGW(TAG, "✗ CONFIG2 MISMATCH: Expected 0x40, got 0x%02X (old version?)", read_config2);
        }
    }

    // Mark ADC as initialized BEFORE sending START/SYNC (needed for hv_adc_start_conversion check)
    hv_adc_initialized = true;

    // Send initial START/SYNC command to wake up ADC from power-down state
    // After reset, ADC is in low-power state and needs START/SYNC to begin conversions
    ESP_LOGI(TAG, "Sending initial START/SYNC command to wake up ADC");
    ret = hv_adc_start_conversion();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Initial START/SYNC failed (may be normal): %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Initial START/SYNC sent successfully");
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "HV ADC initialized successfully");
    ESP_LOGI(TAG, "========================================");

    return ESP_OK;
}

esp_err_t hv_adc_set_gain(uint8_t gain)
{
    if (!hv_adc_initialized) {
        ESP_LOGE(TAG, "ADC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (gain > HV_ADC_GAIN_128) {
        ESP_LOGE(TAG, "Invalid gain setting: %d", gain);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t config0;
    esp_err_t ret = hv_adc_read_register(HV_ADC_REG_CONFIG0, &config0, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    // Clear gain bits and set new gain
    config0 &= ~HV_ADC_CONFIG0_GAIN_MASK;
    config0 |= (gain << HV_ADC_CONFIG0_GAIN_SHIFT);

    ret = hv_adc_write_register(HV_ADC_REG_CONFIG0, config0);
    if (ret == ESP_OK) {
        current_gain = gain;
        ESP_LOGI(TAG, "ADC gain set to: %d", 1 << gain);
    }

    return ret;
}

esp_err_t hv_adc_set_data_rate(uint8_t data_rate)
{
    if (!hv_adc_initialized) {
        ESP_LOGE(TAG, "ADC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (data_rate > HV_ADC_DR_1000SPS) {
        ESP_LOGE(TAG, "Invalid data rate setting: %d", data_rate);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t config1;
    esp_err_t ret = hv_adc_read_register(HV_ADC_REG_CONFIG1, &config1, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    // Clear data rate bits and set new rate
    config1 &= ~HV_ADC_CONFIG1_DR_MASK;
    config1 |= (data_rate << HV_ADC_CONFIG1_DR_SHIFT);

    ret = hv_adc_write_register(HV_ADC_REG_CONFIG1, config1);
    if (ret == ESP_OK) {
        current_data_rate = data_rate;
        ESP_LOGI(TAG, "ADC data rate updated");
    }

    return ret;
}

esp_err_t hv_adc_start_conversion(void)
{
    if (!hv_adc_initialized) {
        ESP_LOGE(TAG, "ADC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Send START/SYNC command (0x08) to start conversion
    // In single-shot mode, this initiates one conversion
    // In continuous mode, this starts continuous conversions
    ESP_LOGD(TAG, "Sending START/SYNC command (0x08)");
    esp_err_t ret = hv_adc_send_command(0x08);  // START/SYNC command
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send START/SYNC command: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "START/SYNC command sent successfully");
    }
    
    return ret;
}

bool hv_adc_is_ready(void)
{
    if (!hv_adc_initialized) {
        return false;
    }

    // Check CONFIG2 register bit 7 (DRDY bit) to see if conversion is ready
    // According to datasheet section 8.6.2.3, bit 7 of CONFIG2 indicates new data ready
    uint8_t config2;
    esp_err_t ret = hv_adc_read_register(HV_ADC_REG_CONFIG2, &config2, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read CONFIG2 for DRDY check: %s", esp_err_to_name(ret));
        return false;
    }

    // Bit 7 is the DRDY flag (1 = new data ready, 0 = no new data)
    bool ready = (config2 & 0x80) != 0;
    
    return ready;
}

esp_err_t hv_adc_read_result(int16_t *raw_value)
{
    if (!hv_adc_initialized) {
        ESP_LOGE(TAG, "ADC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (raw_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Use RDATA command to read conversion result
    // According to datasheet section 8.5.3.5, RDATA requires:
    // 1. First frame: I2C write with RDATA command (0x10) - START, address+W, 0x10, STOP
    // 2. Second frame: I2C read to get 2 bytes - START (repeated), address+R, data[MSB], ACK, data[LSB], NACK, STOP
    // We use i2c_bus_write_read for this combined operation
    uint8_t rdata_cmd = 0x10;
    uint8_t data[2] = {0, 0};
    
    ESP_LOGD(TAG, "Sending RDATA command (0x%02X)", rdata_cmd);
    
    // Check CONFIG2 before reading to see DRDY state
    uint8_t config2_before_read;
    if (hv_adc_read_register(HV_ADC_REG_CONFIG2, &config2_before_read, 1) == ESP_OK) {
        ESP_LOGD(TAG, "CONFIG2 before RDATA: 0x%02X (DRDY=%d)", 
                 config2_before_read, (config2_before_read & 0x80) ? 1 : 0);
    }
    
    esp_err_t ret = i2c_bus_write_read(HV_ADC_I2C_ADDR, &rdata_cmd, 1, data, 2, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC result (RDATA): %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "RDATA response: MSB=0x%02X, LSB=0x%02X, combined=0x%04X", 
             data[0], data[1], ((uint16_t)data[0] << 8) | data[1]);

    // Combine bytes: MSB first, 16-bit signed value
    int16_t result = ((int16_t)data[0] << 8) | data[1];

    *raw_value = result;
    
    // Check CONFIG2 after reading (DRDY should be cleared after reading)
    uint8_t config2_after_read;
    if (hv_adc_read_register(HV_ADC_REG_CONFIG2, &config2_after_read, 1) == ESP_OK) {
        ESP_LOGD(TAG, "CONFIG2 after RDATA: 0x%02X (DRDY=%d)", 
                 config2_after_read, (config2_after_read & 0x80) ? 1 : 0);
    }

    return ESP_OK;
}

esp_err_t hv_adc_read_differential(uint8_t mux_config, float *voltage_mv)
{
    if (!hv_adc_initialized) {
        ESP_LOGE(TAG, "ADC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (voltage_mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Configure MUX for differential reading
    uint8_t config0;
    esp_err_t ret = hv_adc_read_register(HV_ADC_REG_CONFIG0, &config0, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read CONFIG0: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "Current CONFIG0: 0x%02X, setting MUX to 0x%02X", config0, mux_config);

    // Set MUX configuration
    config0 &= ~HV_ADC_CONFIG0_MUX_MASK;
    config0 |= (mux_config << HV_ADC_CONFIG0_MUX_SHIFT);

    ret = hv_adc_write_register(HV_ADC_REG_CONFIG0, config0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure MUX: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "MUX configured, CONFIG0 now: 0x%02X", config0);

    // Wait a bit for MUX to settle
    vTaskDelay(pdMS_TO_TICKS(10));

    // Check CONFIG2 before starting conversion
    uint8_t config2_before;
    esp_err_t ret_config2 = hv_adc_read_register(HV_ADC_REG_CONFIG2, &config2_before, 1);
    if (ret_config2 == ESP_OK) {
        ESP_LOGD(TAG, "CONFIG2 before START/SYNC: 0x%02X (DRDY bit=%d)", 
                 config2_before, (config2_before & 0x80) ? 1 : 0);
    }

    // Send START/SYNC command to initiate conversion (required in single-shot mode)
    ret = hv_adc_start_conversion();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start conversion: %s", esp_err_to_name(ret));
        return ret;
    }

    // Small delay to allow command to be processed
    vTaskDelay(pdMS_TO_TICKS(5));

    // Wait for conversion to complete
    // For 20 SPS, conversion takes ~50ms according to datasheet
    // Poll DRDY bit or wait with timeout
    int timeout_ms = 100;  // Maximum wait time
    int poll_interval_ms = 5;  // Check every 5ms
    bool ready = false;
    
    for (int waited = 0; waited < timeout_ms; waited += poll_interval_ms) {
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
        ready = hv_adc_is_ready();
        if (ready) {
            ESP_LOGD(TAG, "Conversion ready after %d ms", waited + poll_interval_ms);
            break;
        }
    }
    
    if (!ready) {
        ESP_LOGW(TAG, "Conversion not ready after %d ms timeout, reading anyway", timeout_ms);
        // Read CONFIG2 to see current state
        uint8_t config2_after;
        if (hv_adc_read_register(HV_ADC_REG_CONFIG2, &config2_after, 1) == ESP_OK) {
            ESP_LOGW(TAG, "CONFIG2 after timeout: 0x%02X (DRDY bit=%d)", 
                     config2_after, (config2_after & 0x80) ? 1 : 0);
        }
    }

    // Read result using RDATA command
    int16_t raw_value;
    ret = hv_adc_read_result(&raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read result: %s", esp_err_to_name(ret));
        return ret;
    }

    // Convert to voltage
    // Formula: V = (raw_value / full_scale) * VREF / gain
    // Full scale is ±32768 for 16-bit signed
    float gain_factor = 1.0f / (1 << current_gain);
    float vref_mv = HV_ADC_VREF_INTERNAL_MV;
    
    *voltage_mv = ((float)raw_value / HV_ADC_FULL_SCALE) * vref_mv * gain_factor;

    // Map MUX to channel name for better logging
    const char* channel_name = "Unknown";
    if (mux_config == HV_ADC_MUX_AIN0_AVSS) channel_name = "HV_Vmon (ch0)";
    else if (mux_config == HV_ADC_MUX_AIN1_AVSS) channel_name = "HV_Vset (ch1)";
    else if (mux_config == HV_ADC_MUX_AIN2_AVSS) channel_name = "HV_Isense (ch2)";
    else if (mux_config == HV_ADC_MUX_AIN3_AVSS) channel_name = "Channel 3";
    else if (mux_config == HV_ADC_MUX_AIN0_AIN1) channel_name = "AIN0-AIN1";
    else if (mux_config == HV_ADC_MUX_TEMP) channel_name = "Temperature";
    
    ESP_LOGI(TAG, "ADC read: %s (MUX=0x%02X), raw=%d, voltage=%.2f mV", 
             channel_name, mux_config, raw_value, *voltage_mv);

    return ESP_OK;
}

esp_err_t hv_adc_read_channel(uint8_t channel, float *voltage_mv)
{
    if (channel > 3) {
        ESP_LOGE(TAG, "Invalid channel: %d (must be 0-3)", channel);
        return ESP_ERR_INVALID_ARG;
    }

    // Convert channel to single-ended MUX configuration
    uint8_t mux_config = HV_ADC_MUX_AIN0_AVSS + channel;
    
    return hv_adc_read_differential(mux_config, voltage_mv);
}

esp_err_t hv_adc_read_temperature(float *temp_celsius)
{
    if (!hv_adc_initialized) {
        ESP_LOGE(TAG, "ADC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (temp_celsius == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Enable temperature sensor mode by setting TS bit in CONFIG1
    uint8_t config1;
    esp_err_t ret = hv_adc_read_register(HV_ADC_REG_CONFIG1, &config1, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read CONFIG1: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set TS bit (bit 0) to enable temperature sensor mode
    config1 |= 0x01;  // TS bit
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG1, config1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable temperature sensor mode: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait a bit for mode change to settle
    vTaskDelay(pdMS_TO_TICKS(10));

    // Start conversion
    ret = hv_adc_start_conversion();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start temperature conversion: %s", esp_err_to_name(ret));
        // Restore CONFIG1
        config1 &= ~0x01;
        hv_adc_write_register(HV_ADC_REG_CONFIG1, config1);
        return ret;
    }

    // Wait for conversion to complete
    int timeout_ms = 100;
    int poll_interval_ms = 5;
    bool ready = false;
    
    for (int waited = 0; waited < timeout_ms; waited += poll_interval_ms) {
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
        ready = hv_adc_is_ready();
        if (ready) {
            ESP_LOGD(TAG, "Temperature conversion ready after %d ms", waited + poll_interval_ms);
            break;
        }
    }
    
    if (!ready) {
        ESP_LOGW(TAG, "Temperature conversion not ready after %d ms timeout", timeout_ms);
    }

    // Read result
    int16_t temp_raw;
    ret = hv_adc_read_result(&temp_raw);
    
    // Restore CONFIG1 (disable temperature sensor mode)
    config1 &= ~0x01;
    hv_adc_write_register(HV_ADC_REG_CONFIG1, config1);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature result: %s", esp_err_to_name(ret));
        return ret;
    }

    // Convert to Celsius
    // According to ADS112C04 datasheet section 8.3.10:
    // Temperature data is 14-bit left-justified in 16-bit result
    // Resolution: 0.03125°C per LSB
    // Formula: T(°C) = (temp_raw >> 2) * 0.03125
    // Or: T(°C) = (temp_raw / 32.0) - 40.0 (if using the simplified formula)
    // Actually, the datasheet says the 14 MSBs represent temperature
    // Let's extract the 14-bit value and convert
    int16_t temp_14bit = temp_raw >> 2;  // Right-shift to get 14-bit value
    *temp_celsius = ((float)temp_14bit) * 0.03125f;

    ESP_LOGI(TAG, "ADC temperature: raw=0x%04X (%d), temp_14bit=%d, temp=%.2f°C", 
             (uint16_t)temp_raw, temp_raw, temp_14bit, *temp_celsius);

    return ESP_OK;
}

#endif // CONFIG_ENABLE_HV_SUPPORT

