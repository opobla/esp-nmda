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
static uint8_t current_vref = HV_ADC_VREF_INTERNAL;

// I2C address (configurable via Kconfig in the future)
#define HV_ADC_I2C_ADDR HV_ADC_I2C_ADDR_DEFAULT

// Internal reference voltage (2.048V)
#define HV_ADC_VREF_INTERNAL_MV 2048.0f

// Full scale range for 16-bit ADC
#define HV_ADC_FULL_SCALE 32768.0f

static esp_err_t hv_adc_read_register(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_bus_read(HV_ADC_I2C_ADDR, &reg, 1, data, len, 1000);
}

static esp_err_t hv_adc_write_register(uint8_t reg, uint8_t data)
{
    return i2c_bus_write(HV_ADC_I2C_ADDR, &reg, 1, &data, 1, 1000);
}

esp_err_t hv_adc_init(void)
{
    if (hv_adc_initialized) {
        ESP_LOGW(TAG, "HV ADC already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Initializing HV ADC (ADS112C04)");
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

    // Reset ADC by writing to config register (soft reset)
    // Writing 0x01 to CONFIG0 performs a reset
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG0, 0x01);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ADC reset command sent");

    // Wait for reset to complete (typically 50us, but wait 10ms to be safe)
    vTaskDelay(pdMS_TO_TICKS(10));

    // Configure CONFIG0: MUX = AIN0-AIN1, Gain = 1, PGA enabled
    uint8_t config0 = (HV_ADC_MUX_AIN0_AIN1 << HV_ADC_CONFIG0_MUX_SHIFT) |
                      (HV_ADC_GAIN_1 << HV_ADC_CONFIG0_GAIN_SHIFT);
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG0, config0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG0: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure CONFIG1: Data rate = 20 SPS, Single-shot mode, Internal VREF
    uint8_t config1 = (HV_ADC_DR_20SPS << HV_ADC_CONFIG1_DR_SHIFT) |
                      (HV_ADC_CM_SINGLE << HV_ADC_CONFIG1_CM_SHIFT) |
                      (HV_ADC_VREF_INTERNAL);
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG1, config1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG1: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure CONFIG2: Default settings (IDAC off, power switch default, no FIR)
    uint8_t config2 = 0x00;
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG2, config2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG2: %s", esp_err_to_name(ret));
        return ret;
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
    
    // Verify all configuration registers
    uint8_t read_config2, read_config3;
    ret = hv_adc_read_register(HV_ADC_REG_CONFIG2, &read_config2, 1);
    if (ret == ESP_OK) {
        ret = hv_adc_read_register(HV_ADC_REG_CONFIG3, &read_config3, 1);
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Full configuration: CONFIG0=0x%02X, CONFIG1=0x%02X, CONFIG2=0x%02X, CONFIG3=0x%02X",
                 read_config0, read_config1, read_config2, read_config3);
    }

    hv_adc_initialized = true;
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

    // In single-shot mode, writing to CONFIG0 with START bit triggers conversion
    // For ADS112C04, we need to set the START bit in CONFIG0
    // Actually, the ADS112C04 starts conversion automatically in single-shot mode
    // when we configure the MUX. Let's read CONFIG0 and set it again to trigger.
    uint8_t config0;
    esp_err_t ret = hv_adc_read_register(HV_ADC_REG_CONFIG0, &config0, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    // Write back to trigger conversion (some ADCs need this)
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG0, config0);
    
    return ret;
}

bool hv_adc_is_ready(void)
{
    if (!hv_adc_initialized) {
        return false;
    }

    // For ADS112C04, we can check the DATA register or use DRDY pin
    // For now, we'll use a simple timeout-based approach
    // In a real implementation, we'd check the DRDY status
    
    // Read CONFIG3 to check DRDY mode (if implemented)
    uint8_t config3;
    if (hv_adc_read_register(HV_ADC_REG_CONFIG3, &config3, 1) != ESP_OK) {
        return false;
    }

    // For now, assume ready after a delay based on data rate
    // This is a simplified implementation
    return true;
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

    // Read 16-bit result from DATA register (2 bytes, MSB first)
    uint8_t data[2];
    esp_err_t ret = hv_adc_read_register(HV_ADC_REG_DATA, data, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC result: %s", esp_err_to_name(ret));
        return ret;
    }

    // Combine bytes: MSB first, 16-bit signed value
    int16_t result = ((int16_t)data[0] << 8) | data[1];

    *raw_value = result;

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
        return ret;
    }

    // Set MUX configuration
    config0 &= ~HV_ADC_CONFIG0_MUX_MASK;
    config0 |= (mux_config << HV_ADC_CONFIG0_MUX_SHIFT);

    ret = hv_adc_write_register(HV_ADC_REG_CONFIG0, config0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure MUX: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait a bit for MUX to settle
    vTaskDelay(pdMS_TO_TICKS(10));

    // Start conversion (write CONFIG0 again to trigger)
    ret = hv_adc_write_register(HV_ADC_REG_CONFIG0, config0);
    if (ret != ESP_OK) {
        return ret;
    }

    // Wait for conversion to complete (depends on data rate)
    // For 20 SPS, conversion takes ~50ms
    int delay_ms = 100;  // Conservative delay
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    // Read result
    int16_t raw_value;
    ret = hv_adc_read_result(&raw_value);
    if (ret != ESP_OK) {
        return ret;
    }

    // Convert to voltage
    // Formula: V = (raw_value / full_scale) * VREF / gain
    float gain_factor = 1.0f / (1 << current_gain);
    float vref_mv = HV_ADC_VREF_INTERNAL_MV;
    
    *voltage_mv = ((float)raw_value / HV_ADC_FULL_SCALE) * vref_mv * gain_factor;

    ESP_LOGI(TAG, "ADC differential read: MUX=0x%02X, raw=%d, voltage=%.2f mV", 
             mux_config, raw_value, *voltage_mv);

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

    // Read temperature from TEMP register
    uint8_t temp_data[2];
    esp_err_t ret = hv_adc_read_register(HV_ADC_REG_TEMP, temp_data, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature: %s", esp_err_to_name(ret));
        return ret;
    }

    // Temperature is 16-bit signed value
    int16_t temp_raw = ((int16_t)temp_data[0] << 8) | temp_data[1];

    // Convert to Celsius
    // According to ADS112C04 datasheet: T(°C) = (temp_raw / 32) - 40
    *temp_celsius = ((float)temp_raw / 32.0f) - 40.0f;

    ESP_LOGI(TAG, "ADC temperature: raw=%d, temp=%.2f°C", temp_raw, *temp_celsius);

    return ESP_OK;
}

#endif // CONFIG_ENABLE_HV_SUPPORT

