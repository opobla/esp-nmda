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

// I2C address (configurable via Kconfig)
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
    
    // Send RREG command, then read response
    // CRITICAL: Use write_read_repeated_start to guarantee Repeated Start condition
    // ADS112C04 requires Repeated Start - if STOP is sent, it discards the RREG command
    esp_err_t ret = i2c_bus_write_read_repeated_start(
        HV_ADC_I2C_ADDR,
         &rreg_cmd, 
         1, 
         data, 
         len, 
         1000);
    
    if (ret != ESP_OK) {
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
    // IMPORTANT: The register number is encoded in bits 5-2, NOT bits 1-0!
    //   - Register 0: 0x40 | (0 << 2) = 0x40 (0100 0000)
    //   - Register 1: 0x40 | (1 << 2) = 0x44 (0100 0100) <- CONFIG1
    //   - Register 2: 0x40 | (2 << 2) = 0x48 (0100 1000) <- CONFIG2
    //   - Register 3: 0x40 | (3 << 2) = 0x4C (0100 1100) <- CONFIG3
    // 
    // Common error: Sending 0x40 for register 1 would write to register 0 instead!
    // According to datasheet section 8.5.3.7, WREG command format is: 0100 rrxx
    uint8_t wreg_cmd = HV_ADC_CMD_WREG | (reg << 2);
    
    // Verify the command is correct (sanity check)
    uint8_t expected_cmd = 0x40 | (reg << 2);
    if (wreg_cmd != expected_cmd) {
        ESP_LOGE(TAG, "WREG command calculation error: reg=%d, got 0x%02X, expected 0x%02X", 
                 reg, wreg_cmd, expected_cmd);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Prepare data: [WREG command, data byte]
    // Note: WREG command already includes register address in bits 5-2
    uint8_t write_buffer[2] = {wreg_cmd, data};
    
    // Send WREG command and data in one transaction
    ESP_LOGI(TAG, 
        "[WREG] Writing register %d: sending WREG command 0x%02X with data 0x%02X", 
        reg, wreg_cmd, data);
    esp_err_t ret = i2c_bus_write(HV_ADC_I2C_ADDR, write_buffer, 2, 1000);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WREG reg %d: FAILED: %s", reg, esp_err_to_name(ret));
    } else {
        // I2C write succeeded, but this doesn't guarantee the ADC accepted the command
        // The ADC should ACK the transaction, but we can't verify that from here
        ESP_LOGI(TAG, "WREG reg %d: I2C transaction completed (ACK received)", reg);
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
    ESP_LOGI(TAG, "Sending command 0x%02X to ADC at address 0x%02X", cmd, HV_ADC_I2C_ADDR);
    
    // ADS112C04 commands already include all necessary information
    esp_err_t ret = i2c_bus_write(HV_ADC_I2C_ADDR, &cmd, 1, 1000);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Command 0x%02X failed: %s", cmd, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Command 0x%02X sent successfully", cmd);
    }
    return ret;
}

esp_err_t hv_adc_init(void)
{
    esp_err_t ret;

    if (hv_adc_initialized) {
        ESP_LOGW(TAG, "HV ADC already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Initializing HV ADC (ADS112C04)");
    ESP_LOGI(TAG, "Driver Version: v2.0 - WREG/RREG + DCNT fix");
    ESP_LOGI(TAG, "I2C Address: 0x%02X", HV_ADC_I2C_ADDR);
    ESP_LOGI(TAG, "========================================");

    // Reset ADC using RESET command (0x06 or 0x07)
    // According to datasheet section 8.5.3.2, RESET command is 0000 011x
    ESP_LOGI(TAG, "Sending RESET command (0x%02X)", HV_ADC_CMD_RESET);
    ret = hv_adc_send_command(HV_ADC_CMD_RESET);  // RESET command
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send RESET command: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ADC reset command sent");

    // Wait for reset to complete (typically 50us, but wait longer to be safe)
    // IMPORTANT: Some ADCs need more time after reset before accepting register writes
    vTaskDelay(pdMS_TO_TICKS(50));  // Increased from 10ms to 50ms
    
    // Verify ADC is ready by reading all registers after reset (all should be 0x00)
    uint8_t regs_after_reset[4];
    ESP_LOGI(TAG, "Reading all registers after RESET:");
    for (int i = 0; i < 4; i++) {
        if (hv_adc_read_register(i, &regs_after_reset[i], 1) == ESP_OK) {
            ESP_LOGI(TAG, "  CONFIG%d: 0x%02X (expected 0x00)", i, regs_after_reset[i]);
        } else {
            ESP_LOGW(TAG, "  Failed to read CONFIG%d after RESET", i);
            regs_after_reset[i] = 0xFF;  // Mark as error
        }
    }
 
    // Configure CONFIG0: MUX = AIN0-AIN1, Gain = 1, PGA enabled
    // Write a NON-ZERO value to verify the write actually works
    uint8_t config0 = (HV_ADC_MUX_AIN0_AIN1 << HV_ADC_CONFIG0_MUX_SHIFT) |
                      (HV_ADC_GAIN_1 << HV_ADC_CONFIG0_GAIN_SHIFT);
    // config0 should be 0x00 for this configuration, but let's try 0x10 to test
    // Actually, let's keep it as calculated but verify it's not 0x00
    ESP_LOGI(TAG, "Writing CONFIG0: 0x%02X", config0);
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG0, config0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG0: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure CONFIG1: Data rate = 20 SPS, Single-shot mode, External VREF
    // NOTE: Using EXTERNAL VREF as per hardware design
    // CONFIG1 bits: [DR(3)][CM(2)][BCS(1)][VREF(2)]
    // VREF bits 1-0: 0x01 = External reference
    uint8_t config1 = (HV_ADC_DR_20SPS << HV_ADC_CONFIG1_DR_SHIFT) |
                      (HV_ADC_CM_SINGLE << HV_ADC_CONFIG1_CM_SHIFT) |
                      (HV_ADC_VREF_EXTERNAL);
    // config1 should now be: DR=0x00<<5 | CM=0x00<<3 | VREF=0x01 = 0x01
    ESP_LOGI(TAG, "Writing CONFIG1: 0x%02X (DR=20SPS, CM=single, VREF=external)", config1);
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG1, config1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG1: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Verify CONFIG1 was written correctly - try multiple times
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t config1_verify;
    bool config1_ok = false;
    for (int retry = 0; retry < 3; retry++) {
        if (hv_adc_read_register(HV_ADC_REG_CONFIG1, &config1_verify, 1) == ESP_OK) {
            ESP_LOGI(TAG, "CONFIG1 read attempt %d: 0x%02X (wrote 0x%02X)", retry + 1, config1_verify, config1);
            if (config1_verify == config1) {
                ESP_LOGI(TAG, "CONFIG1 verified: 0x%02X", config1_verify);
                config1_ok = true;
                break;
            }
            if (retry < 2) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        } else {
            ESP_LOGW(TAG, "CONFIG1 read attempt %d: FAILED", retry + 1);
        }
    }
    if (!config1_ok) {
        ESP_LOGW(TAG, "CONFIG1 verification failed after 3 attempts: wrote 0x%02X, final read 0x%02X", 
                 config1, config1_verify);
    }
    
    // Configure CONFIG3: IDAC routing (both disabled by default)
    // According to datasheet section 8.6.2.4, CONFIG3 defaults to 0x00
    // We'll write it explicitly to ensure it's set correctly
    uint8_t config3 = 0x00;  // Both IDACs disabled (default)
    ESP_LOGI(TAG, "Writing CONFIG3: 0x%02X (IDACs disabled)", config3);
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG3, config3);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG3: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    
    // Configure CONFIG2: Enable data counter (DCNT=1, bit 6) to make DRDY available in CONFIG2 bit 7
    // IMPORTANT: Write CONFIG2 AFTER CONFIG3, as some ADCs require registers to be written in order
    // According to datasheet section 8.6.2.3:
    // - Bit 7 (DRDY): Data ready flag (read-only, set by ADC when conversion complete)
    // - Bit 6 (DCNT): Data counter enable (1 = enable, makes DRDY available in bit 7)
    // - Bits 5-4: CRC mode (00 = disabled)
    // - Bit 3: BCS = 0 (burn-out current sources off)
    // - Bits 2-0: IDAC current setting (000 = off)
    uint8_t config2 = HV_ADC_CONFIG2_DCNT;  // 0x40 - Enable data counter to get DRDY in CONFIG2
    ESP_LOGI(TAG, "Writing CONFIG2: 0x%02X (DCNT=1, DRDY polling enabled)", config2);
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG2, config2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG2: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait longer for CONFIG2 to settle (some ADCs need more time for certain registers)
    vTaskDelay(pdMS_TO_TICKS(100));  // Increased from 50ms to 100ms
    
    // Verify CONFIG2 was written correctly (note: bit 7 DRDY is read-only, so mask it out)
    uint8_t config2_verify;
    if (hv_adc_read_register(HV_ADC_REG_CONFIG2, &config2_verify, 1) == ESP_OK) {
        uint8_t config2_writable = config2_verify & 0x7F;  // Mask out read-only DRDY bit
        if (config2_writable != config2) {
            ESP_LOGW(TAG, "CONFIG2 verification failed: wrote 0x%02X, read 0x%02X (masked: 0x%02X)", 
                     config2, config2_verify, config2_writable);
        } else {
            ESP_LOGI(TAG, "CONFIG2 verified: 0x%02X (DRDY bit may vary)", config2_verify);
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
    ESP_LOGI(TAG, "Sending START/SYNC command (0x08)");
    esp_err_t ret = hv_adc_send_command(0x08);  // START/SYNC command
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send START/SYNC command: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "START/SYNC command sent successfully");
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
    // 1. First frame: I2C write with RDATA command (0x10) - START, address+W, 0x10
    // 2. Second frame: I2C read to get 2 bytes - REPEATED START (not STOP+START), address+R, data[MSB], ACK, data[LSB], NACK, STOP
    // CRITICAL: ADS112C04 requires Repeated Start - if STOP is sent, it discards the RDATA command
    // We MUST use i2c_bus_write_read_repeated_start() to guarantee Repeated Start
    uint8_t rdata_cmd = 0x10;
    uint8_t data[2] = {0, 0};
    
    ESP_LOGI(TAG, "Sending RDATA command (0x%02X)", rdata_cmd);
    
    // Check CONFIG2 before reading to see DRDY state
    uint8_t config2_before_read;
    if (hv_adc_read_register(HV_ADC_REG_CONFIG2, &config2_before_read, 1) == ESP_OK) {
        ESP_LOGI(TAG, "CONFIG2 before RDATA: 0x%02X (DRDY=%d)", 
                 config2_before_read, (config2_before_read & 0x80) ? 1 : 0);
    }
    
    // CRITICAL: Use write_read_repeated_start to guarantee Repeated Start condition
    // This uses i2c_master_transmit_receive() which guarantees Repeated Start
    esp_err_t ret = i2c_bus_write_read_repeated_start(HV_ADC_I2C_ADDR, &rdata_cmd, 1, data, 2, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC result (RDATA): %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "RDATA response: MSB=0x%02X, LSB=0x%02X, combined=0x%04X", 
             data[0], data[1], ((uint16_t)data[0] << 8) | data[1]);

    // Combine bytes: MSB first, 16-bit signed value
    // IMPORTANT: Combine as unsigned first, then cast to signed to avoid sign extension issues
    // This ensures we don't accidentally extend the sign of the MSB byte
    uint16_t combined = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
    int16_t result = (int16_t)combined;
    
    ESP_LOGI(TAG, "Raw value combination: MSB=0x%02X, LSB=0x%02X, combined=0x%04X, result=%d", 
             data[0], data[1], combined, result);

    *raw_value = result;
    
    // Check CONFIG2 after reading (DRDY should be cleared after reading)
    uint8_t config2_after_read;
    if (hv_adc_read_register(HV_ADC_REG_CONFIG2, &config2_after_read, 1) == ESP_OK) {
        ESP_LOGI(TAG, "CONFIG2 after RDATA: 0x%02X (DRDY=%d)", 
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

    // Update current_gain from CONFIG0 to keep it in sync
    uint8_t gain_from_config0 = (config0 & HV_ADC_CONFIG0_GAIN_MASK) >> HV_ADC_CONFIG0_GAIN_SHIFT;
    if (gain_from_config0 != current_gain) {
        ESP_LOGW(TAG, "Gain mismatch: current_gain=%d, CONFIG0 gain=%d, updating", current_gain, gain_from_config0);
        current_gain = gain_from_config0;
    }

    ESP_LOGI(TAG, "Current CONFIG0: 0x%02X, setting MUX to 0x%02X (single-ended channel), gain=%d", 
             config0, mux_config, current_gain);

    // Set MUX configuration (preserve gain bits)
    // MUX[3:0] values for single-ended mode:
    //   0x8 = AIN0-AVSS (channel 0)
    //   0x9 = AIN1-AVSS (channel 1)
    //   0xA = AIN2-AVSS (channel 2)
    //   0xB = AIN3-AVSS (channel 3)
    // These values are placed in bits [7:4] of CONFIG0
    config0 &= ~HV_ADC_CONFIG0_MUX_MASK;
    uint8_t mux_value_in_config0 = (mux_config << HV_ADC_CONFIG0_MUX_SHIFT);
    config0 |= mux_value_in_config0;
    
    ESP_LOGI(TAG, "MUX calculation: mux_config=0x%02X, shifted=0x%02X, CONFIG0 will be: 0x%02X", 
             mux_config, mux_value_in_config0, config0);

    ret = hv_adc_write_register(HV_ADC_REG_CONFIG0, config0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure MUX: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify CONFIG0 was written correctly
    uint8_t config0_verify;
    vTaskDelay(pdMS_TO_TICKS(5));
    if (hv_adc_read_register(HV_ADC_REG_CONFIG0, &config0_verify, 1) == ESP_OK) {
        uint8_t mux_read_back = (config0_verify & HV_ADC_CONFIG0_MUX_MASK) >> HV_ADC_CONFIG0_MUX_SHIFT;
        ESP_LOGI(TAG, "MUX configured: CONFIG0=0x%02X, MUX[3:0] read back=0x%X (expected 0x%X)", 
                 config0_verify, mux_read_back, mux_config);
        if (mux_read_back != mux_config) {
            ESP_LOGW(TAG, "MUX mismatch: wrote 0x%X, read 0x%X", mux_config, mux_read_back);
        }
    }

    // Wait a bit for MUX to settle
    vTaskDelay(pdMS_TO_TICKS(10));

    // Check CONFIG2 before starting conversion
    uint8_t config2_before;
    esp_err_t ret_config2 = hv_adc_read_register(HV_ADC_REG_CONFIG2, &config2_before, 1);
    if (ret_config2 == ESP_OK) {
        ESP_LOGI(TAG, "CONFIG2 before START/SYNC: 0x%02X (DRDY bit=%d)", 
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
    int timeout_ms = 200;  // Increased timeout to 200ms for 20 SPS
    int poll_interval_ms = 5;  // Check every 5ms
    bool ready = false;
    
    for (int waited = 0; waited < timeout_ms; waited += poll_interval_ms) {
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
        ready = hv_adc_is_ready();
        if (ready) {
            ESP_LOGI(TAG, "Conversion ready after %d ms", waited + poll_interval_ms);
            break;
        }
    }
    
    if (!ready) {
        ESP_LOGE(TAG, "Conversion not ready after %d ms timeout - DRDY never set!", timeout_ms);
        // Read CONFIG2 to see current state
        uint8_t config2_after;
        if (hv_adc_read_register(HV_ADC_REG_CONFIG2, &config2_after, 1) == ESP_OK) {
            ESP_LOGE(TAG, "CONFIG2 after timeout: 0x%02X (DRDY bit=%d)", 
                     config2_after, (config2_after & 0x80) ? 1 : 0);
        }
        // Don't read if DRDY is not set - return error instead
        return ESP_ERR_TIMEOUT;
    }

    // Read result using RDATA command (only if DRDY was set)
    int16_t raw_value;
    ret = hv_adc_read_result(&raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read result: %s", esp_err_to_name(ret));
        return ret;
    }

    // Convert to voltage
    // Formula: V = (raw_value / full_scale) * VREF / gain
    // Full scale is ±32768 for 16-bit signed
    // Gain factor: if gain=0 (GAIN_1), factor=1; if gain=1 (GAIN_2), factor=0.5, etc.
    float gain_factor = 1.0f / (1 << current_gain);
    float vref_mv = HV_ADC_VREF_INTERNAL_MV;
    
    ESP_LOGI(TAG, "Voltage calculation: raw=%d, gain=%d, gain_factor=%.4f, vref=%.1f mV", 
             raw_value, current_gain, gain_factor, vref_mv);
    
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

    // Save current CONFIG0 to restore it later
    uint8_t saved_config0;
    esp_err_t ret = hv_adc_read_register(HV_ADC_REG_CONFIG0, &saved_config0, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read CONFIG0: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[TEMP] Saved CONFIG0: 0x%02X", saved_config0);

    // Enable temperature sensor mode by setting TS bit in CONFIG1
    uint8_t config1;
    ret = hv_adc_read_register(HV_ADC_REG_CONFIG1, &config1, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read CONFIG1: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "[TEMP] Initial CONFIG1 read: 0x%02X", config1);

    // Check CONFIG2 to see ADC state before writing
    uint8_t config2_before;
    if (hv_adc_read_register(HV_ADC_REG_CONFIG2, &config2_before, 1) == ESP_OK) {
        ESP_LOGI(TAG, "[TEMP] CONFIG2 before write: 0x%02X (DRDY=%d, DCNT=%d)", 
                 config2_before, 
                 (config2_before & 0x80) ? 1 : 0,
                 (config2_before & 0x40) ? 1 : 0);
    }

    // If CONFIG1 is 0x00, it means the register was never written or was reset
    // This could happen if the ADC was reset or if initialization failed
    if (config1 == 0x00) {
        ESP_LOGW(TAG, "[TEMP] CONFIG1 is 0x00 - ADC may need re-initialization or was reset");
        // Try to restore CONFIG1 to expected value first
        uint8_t config1_restore = (HV_ADC_DR_20SPS << HV_ADC_CONFIG1_DR_SHIFT) |
                                  (HV_ADC_CM_SINGLE << HV_ADC_CONFIG1_CM_SHIFT) |
                                  (HV_ADC_VREF_EXTERNAL);
        ESP_LOGI(TAG, "[TEMP] Restoring CONFIG1 to 0x%02X before setting TS bit", config1_restore);
        ret = hv_adc_write_register(HV_ADC_REG_CONFIG1, config1_restore);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restore CONFIG1: %s", esp_err_to_name(ret));
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        // Read back to verify
        if (hv_adc_read_register(HV_ADC_REG_CONFIG1, &config1, 1) == ESP_OK) {
            ESP_LOGI(TAG, "[TEMP] CONFIG1 after restore: 0x%02X", config1);
        }
    }

    // Set TS bit (bit 0) to enable temperature sensor mode
    uint8_t config1_before_write = config1;
    config1 |= 0x01;  // TS bit
    ESP_LOGI(TAG, "[TEMP] Setting TS bit: CONFIG1 0x%02X -> 0x%02X", config1_before_write, config1);
    
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG1, config1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable temperature sensor mode: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait a bit for register write to complete
    // According to datasheet, WREG command resets digital filter and restarts conversion
    // Give it more time to settle
    vTaskDelay(pdMS_TO_TICKS(10));

    // Verify that TS bit was set correctly
    uint8_t config1_verify;
    ret = hv_adc_read_register(HV_ADC_REG_CONFIG1, &config1_verify, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read CONFIG1 for verification: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "[TEMP] CONFIG1 after write: 0x%02X (wrote 0x%02X)", config1_verify, config1);
    
    if ((config1_verify & 0x01) != 0x01) {
        ESP_LOGE(TAG, "TS bit verification failed: CONFIG1=0x%02X (expected TS=1, wrote 0x%02X)", 
                 config1_verify, config1);
        // Try reading again to see if it's a timing issue
        vTaskDelay(pdMS_TO_TICKS(10));
        uint8_t config1_retry;
        if (hv_adc_read_register(HV_ADC_REG_CONFIG1, &config1_retry, 1) == ESP_OK) {
            ESP_LOGE(TAG, "[TEMP] CONFIG1 retry read: 0x%02X", config1_retry);
        }
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "TS bit verified: CONFIG1=0x%02X (TS=1)", config1_verify);


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
    int timeout_ms = 500;
    int poll_interval_ms = 5;
    bool ready = false;
    
    for (int waited = 0; waited < timeout_ms; waited += poll_interval_ms) {
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
        ready = hv_adc_is_ready();
        if (ready) {
            ESP_LOGI(TAG, "Temperature conversion ready after %d ms", waited + poll_interval_ms);
            break;
        }
    }
    
    if (!ready) {
        ESP_LOGW(TAG, "Temperature conversion not ready after %d ms timeout", timeout_ms);
    }

    // Read result
    int16_t temp_raw;
    ret = hv_adc_read_result(&temp_raw);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature result: %s", esp_err_to_name(ret));
        // Try to restore anyway
        config1 &= ~0x01;
        hv_adc_write_register(HV_ADC_REG_CONFIG1, config1);
        hv_adc_write_register(HV_ADC_REG_CONFIG0, saved_config0);
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

    // Restore CONFIG1 (disable temperature sensor mode)
    config1 &= ~0x01;
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG1, config1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[TEMP] Failed to restore CONFIG1: %s", esp_err_to_name(ret));
    }
    
    // Restore CONFIG0 to previous value
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG0, saved_config0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[TEMP] Failed to restore CONFIG0: %s", esp_err_to_name(ret));
    }
    
    // Wait for register writes to complete
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Clear ADC buffer by starting a new conversion and discarding the result
    // This ensures the next channel read doesn't get stale temperature data
    ESP_LOGI(TAG, "[TEMP] Clearing ADC buffer after temperature read");
    ret = hv_adc_start_conversion();
    if (ret == ESP_OK) {
        // Wait for conversion to complete
        int clear_timeout_ms = 100;
        int clear_poll_interval_ms = 5;
        bool clear_ready = false;
        for (int waited = 0; waited < clear_timeout_ms; waited += clear_poll_interval_ms) {
            vTaskDelay(pdMS_TO_TICKS(clear_poll_interval_ms));
            clear_ready = hv_adc_is_ready();
            if (clear_ready) {
                break;
            }
        }
        
        // Read and discard the result to clear the buffer
        int16_t discard_value;
        if (hv_adc_read_result(&discard_value) == ESP_OK) {
            ESP_LOGI(TAG, "[TEMP] ADC buffer cleared (discarded value: %d)", discard_value);
        }
    }

    return ESP_OK;
}

#endif // CONFIG_ENABLE_HV_SUPPORT

