#include "spl06.h"

#ifdef CONFIG_ENABLE_SPL06

#include "i2c_bus.h"
#include "esp32-libs.h"
#include <math.h>

static const char *TAG = "SPL06";

// Calibration coefficients (loaded from sensor)
static struct {
    int16_t c0;
    int16_t c1;
    int32_t c00;
    int32_t c10;
    int16_t c01;
    int16_t c11;
    int16_t c20;
    int16_t c21;
    int16_t c30;
} calib_coeffs;

static bool spl06_initialized = false;
static bool calib_loaded = false;

// I2C address
#define SPL06_I2C_ADDR CONFIG_SPL06_I2C_ADDRESS

static esp_err_t spl06_read_register(uint8_t reg, uint8_t *data, size_t len)
{
    esp_err_t ret = i2c_bus_read(SPL06_I2C_ADDR, &reg, 1, data, len, 1000);
    
    // Debug: log register reads for temperature registers
    if (reg >= SPL06_REG_TMP_B2 && reg <= SPL06_REG_TMP_B0 && len <= 3) {
        static int reg_debug_count = 0;
        if (reg_debug_count < 3) {
            ESP_LOGI(TAG, "Read register 0x%02X: ", reg);
            for (size_t i = 0; i < len; i++) {
                ESP_LOGI(TAG, "  data[%d]=0x%02X", i, data[i]);
            }
            reg_debug_count++;
        }
    }
    
    return ret;
}

static esp_err_t spl06_write_register(uint8_t reg, uint8_t data)
{
    return i2c_bus_write(SPL06_I2C_ADDR, &reg, 1, &data, 1, 1000);
}

static esp_err_t spl06_read_calibration(void)
{
    uint8_t coeff_data[18];
    esp_err_t ret;

    // Read calibration coefficients starting from register 0x10
    ret = spl06_read_register(SPL06_REG_COEF, coeff_data, 18);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration coefficients");
        return ret;
    }

    // Parse coefficients (little-endian)
    calib_coeffs.c0 = (int16_t)((coeff_data[0] << 4) | ((coeff_data[1] >> 4) & 0x0F));
    if (calib_coeffs.c0 & 0x800) {
        calib_coeffs.c0 |= 0xF000; // Sign extend
    }

    calib_coeffs.c1 = (int16_t)(((coeff_data[1] & 0x0F) << 8) | coeff_data[2]);
    if (calib_coeffs.c1 & 0x800) {
        calib_coeffs.c1 |= 0xF000; // Sign extend
    }

    calib_coeffs.c00 = (int32_t)((coeff_data[3] << 12) | (coeff_data[4] << 4) | ((coeff_data[5] >> 4) & 0x0F));
    if (calib_coeffs.c00 & 0x80000) {
        calib_coeffs.c00 |= 0xFFF00000; // Sign extend
    }

    calib_coeffs.c10 = (int32_t)(((coeff_data[5] & 0x0F) << 16) | (coeff_data[6] << 8) | coeff_data[7]);
    if (calib_coeffs.c10 & 0x80000) {
        calib_coeffs.c10 |= 0xFFF00000; // Sign extend
    }

    calib_coeffs.c01 = (int16_t)((coeff_data[8] << 8) | coeff_data[9]);
    calib_coeffs.c11 = (int16_t)((coeff_data[10] << 8) | coeff_data[11]);
    calib_coeffs.c20 = (int16_t)((coeff_data[12] << 8) | coeff_data[13]);
    calib_coeffs.c21 = (int16_t)((coeff_data[14] << 8) | coeff_data[15]);
    calib_coeffs.c30 = (int16_t)((coeff_data[16] << 8) | coeff_data[17]);

    calib_loaded = true;
    ESP_LOGI(TAG, "Calibration coefficients loaded");
    ESP_LOGI(TAG, "c0=%d, c1=%d, c00=%ld, c10=%ld", 
             calib_coeffs.c0, calib_coeffs.c1, calib_coeffs.c00, calib_coeffs.c10);
    ESP_LOGI(TAG, "c01=%d, c11=%d, c20=%d, c21=%d, c30=%d",
             calib_coeffs.c01, calib_coeffs.c11, calib_coeffs.c20, calib_coeffs.c21, calib_coeffs.c30);

    return ESP_OK;
}

esp_err_t spl06_init(void)
{
    if (spl06_initialized) {
        ESP_LOGW(TAG, "SPL06 already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting SPL06 initialization (I2C address: 0x%02X)", SPL06_I2C_ADDR);
    
    uint8_t product_id;
    esp_err_t ret;

    // Read product ID to verify sensor presence
    ESP_LOGI(TAG, "Reading product ID from register 0x%02X", SPL06_REG_PRODUCT_ID);
    ret = spl06_read_register(SPL06_REG_PRODUCT_ID, &product_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read product ID: %s (0x%X)", esp_err_to_name(ret), ret);
        ESP_LOGE(TAG, "Check I2C connection and address (0x%02X)", SPL06_I2C_ADDR);
        return ret;
    }
    
    ESP_LOGI(TAG, "Product ID read: 0x%02X", product_id);

    if (product_id != SPL06_PRODUCT_ID) {
        ESP_LOGE(TAG, "Invalid product ID: 0x%02X (expected 0x%02X)", product_id, SPL06_PRODUCT_ID);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "SPL06-001 detected (Product ID: 0x%02X)", product_id);

    // Soft reset
    ret = spl06_write_register(SPL06_REG_RESET, 0x09);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset sensor");
        return ret;
    }

    // Wait for sensor to be ready (100ms)
    vTaskDelay(pdMS_TO_TICKS(100));

    // Configure pressure measurement: 4 samples per second, 8x oversampling
    // PRS_CFG: Rate=4 (010), PRC=8 (1000) -> 0x24
    ret = spl06_write_register(SPL06_REG_PRS_CFG, 0x24);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure pressure");
        return ret;
    }
    ESP_LOGI(TAG, "Pressure configured: Rate=4, PRC=8");

    // Configure temperature measurement: 4 samples per second, 8x oversampling
    // TMP_CFG: Rate=4 (010), PRC=8 (1000), no external sensor -> 0x24
    ret = spl06_write_register(SPL06_REG_TMP_CFG, 0x24);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure temperature");
        return ret;
    }
    ESP_LOGI(TAG, "Temperature configured: Rate=4, PRC=8");

    // Configure measurement: continuous pressure and temperature
    // MEAS_CFG: Continuous mode (0x07 = measure both pressure and temperature continuously)
    // Note: According to datasheet, 0x07 means:
    //   - Bit 0-2: Measurement control = 111 (continuous pressure and temperature)
    ret = spl06_write_register(SPL06_REG_MEAS_CFG, 0x07);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure measurement");
        return ret;
    }
    ESP_LOGI(TAG, "Measurement configured: Continuous mode (0x07)");
    
    // Wait for first measurement to be ready
    // With oversampling 8x and rate 4, first measurement takes ~200-250ms
    ESP_LOGI(TAG, "Waiting for first measurement...");
    int init_timeout = 50; // 50 attempts = 500ms
    while (!spl06_is_ready() && init_timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (init_timeout <= 0) {
        ESP_LOGW(TAG, "Sensor not ready after initialization wait");
    } else {
        ESP_LOGI(TAG, "Sensor ready after initialization");
    }

    // Load calibration coefficients
    ret = spl06_read_calibration();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load calibration coefficients");
        return ret;
    }

    spl06_initialized = true;
    ESP_LOGI(TAG, "SPL06-001 initialized successfully");
    
    // Wait for sensor to stabilize and discard first reading
    vTaskDelay(pdMS_TO_TICKS(500));
    float dummy_pressure, dummy_temp;
    spl06_read_both(&dummy_pressure, &dummy_temp);
    ESP_LOGI(TAG, "Discarded first reading (stabilization)");

    return ESP_OK;
}

bool spl06_is_ready(void)
{
    if (!spl06_initialized) {
        return false;
    }

    uint8_t meas_cfg;
    if (spl06_read_register(SPL06_REG_MEAS_CFG, &meas_cfg, 1) != ESP_OK) {
        return false;
    }

    // Check if both pressure and temperature are ready
    bool ready = (meas_cfg & (SPL06_MEAS_CFG_PRS_RDY | SPL06_MEAS_CFG_TMP_RDY)) == 
                 (SPL06_MEAS_CFG_PRS_RDY | SPL06_MEAS_CFG_TMP_RDY);
    
    // Debug: log ready status
    static int ready_debug_count = 0;
    if (ready_debug_count < 5) {
        ESP_LOGI(TAG, "Sensor ready check: meas_cfg=0x%02X, PRS_RDY=%d, TMP_RDY=%d, ready=%d",
                 meas_cfg, 
                 (meas_cfg & SPL06_MEAS_CFG_PRS_RDY) ? 1 : 0,
                 (meas_cfg & SPL06_MEAS_CFG_TMP_RDY) ? 1 : 0,
                 ready ? 1 : 0);
        ready_debug_count++;
    }
    
    return ready;
}

static int32_t spl06_read_raw_pressure(void)
{
    uint8_t data[3];
    if (spl06_read_register(SPL06_REG_PSR_B2, data, 3) != ESP_OK) {
        return 0;
    }

    // 24-bit signed value
    int32_t raw = ((int32_t)data[0] << 16) | ((int32_t)data[1] << 8) | data[2];
    if (raw & 0x800000) {
        raw |= 0xFF000000; // Sign extend
    }

    return raw;
}

static int32_t spl06_read_raw_temperature(void)
{
    uint8_t b2, b1, b0;
    
    // Read temperature registers individually to ensure correct reading
    // SPL06-001 uses register addresses 0x03 (TMP_B2), 0x04 (TMP_B1), 0x05 (TMP_B0)
    if (spl06_read_register(SPL06_REG_TMP_B2, &b2, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read TMP_B2 register");
        return 0;
    }
    if (spl06_read_register(SPL06_REG_TMP_B1, &b1, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read TMP_B1 register");
        return 0;
    }
    if (spl06_read_register(SPL06_REG_TMP_B0, &b0, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read TMP_B0 register");
        return 0;
    }

    // 24-bit signed value in two's complement
    // TMP_B2 is MSB (bits 23-16), TMP_B1 is middle (bits 15-8), TMP_B0 is LSB (bits 7-0)
    int32_t raw = ((int32_t)b2 << 16) | ((int32_t)b1 << 8) | b0;
    
    // Sign extend from 24-bit to 32-bit
    if (raw & 0x800000) {
        raw |= 0xFF000000; // Sign extend for negative values
    }

    // Debug: log raw bytes
    ESP_LOGI(TAG, "Temp raw: B2=0x%02X, B1=0x%02X, B0=0x%02X -> 0x%06lX (%ld)",
             b2, b1, b0, (unsigned long)raw & 0xFFFFFF, raw);
    
    // Validate raw value (typical range for temperature is -50000 to 50000)
    if (raw > 50000 || raw < -50000) {
        ESP_LOGW(TAG, "Temperature raw value out of expected range: %ld (expected -50000 to 50000)", raw);
        ESP_LOGW(TAG, "This might indicate sensor not ready or incorrect reading");
    }

    return raw;
}

esp_err_t spl06_read_pressure(float *pressure_pa)
{
    if (!spl06_initialized || !calib_loaded) {
        ESP_LOGE(TAG, "SPL06 not initialized or calibration not loaded");
        return ESP_ERR_INVALID_STATE;
    }

    if (pressure_pa == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Wait for measurement to be ready
    int timeout = 100; // 100 attempts
    while (!spl06_is_ready() && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (timeout <= 0) {
        ESP_LOGE(TAG, "Timeout waiting for pressure measurement");
        return ESP_ERR_TIMEOUT;
    }

    int32_t raw_pressure = spl06_read_raw_pressure();
    int32_t raw_temperature = spl06_read_raw_temperature();

    // Scale factors (for 8x oversampling)
    float pscal = 253952.0f; // 2^20 / 8
    float tscal = 524288.0f; // 2^20

    // Normalized values
    float p_raw_sc = (float)raw_pressure / pscal;
    float t_raw_sc = (float)raw_temperature / tscal;

    // Calculate pressure using calibration coefficients
    float pressure = (float)calib_coeffs.c00 + 
                     p_raw_sc * ((float)calib_coeffs.c10 + p_raw_sc * ((float)calib_coeffs.c20 + p_raw_sc * (float)calib_coeffs.c30)) +
                     t_raw_sc * (float)calib_coeffs.c01 +
                     t_raw_sc * p_raw_sc * ((float)calib_coeffs.c11 + p_raw_sc * (float)calib_coeffs.c21);

    *pressure_pa = pressure;
    return ESP_OK;
}

esp_err_t spl06_read_temperature(float *temp_celsius)
{
    if (!spl06_initialized || !calib_loaded) {
        ESP_LOGE(TAG, "SPL06 not initialized or calibration not loaded");
        return ESP_ERR_INVALID_STATE;
    }

    if (temp_celsius == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Wait for measurement to be ready
    int timeout = 100; // 100 attempts
    while (!spl06_is_ready() && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (timeout <= 0) {
        ESP_LOGE(TAG, "Timeout waiting for temperature measurement");
        return ESP_ERR_TIMEOUT;
    }

    int32_t raw_temperature = spl06_read_raw_temperature();

    // Scale factor kT for temperature (for 8x oversampling, PRC=8)
    // kT = 2^20 = 524288
    float tscal = 524288.0f;

    // Normalized value: Traw_sc = Traw / kT
    float t_raw_sc = (float)raw_temperature / tscal;

    // Calculate temperature using calibration coefficients
    // Formula from datasheet: T = c0 * 0.5 + c1 * Traw_sc
    float c0_term = (float)calib_coeffs.c0 * 0.5f;
    float c1_term = (float)calib_coeffs.c1 * t_raw_sc;
    float temperature = c0_term + c1_term;

    // Debug logging
    ESP_LOGI(TAG, "Temp calc: raw=%ld, t_raw_sc=%.6f, c0=%d, c1=%d", 
             raw_temperature, t_raw_sc, calib_coeffs.c0, calib_coeffs.c1);
    ESP_LOGI(TAG, "Temp calc: c0*0.5=%.2f, c1*t_raw_sc=%.2f, result=%.2f°C",
             c0_term, c1_term, temperature);
    
    // Check if raw value seems reasonable (typical range is -100000 to 100000)
    if (raw_temperature > 100000 || raw_temperature < -100000) {
        ESP_LOGW(TAG, "Temperature raw value seems out of range: %ld", raw_temperature);
    }

    *temp_celsius = temperature;
    return ESP_OK;
}

esp_err_t spl06_read_both(float *pressure_pa, float *temp_celsius)
{
    if (!spl06_initialized || !calib_loaded) {
        ESP_LOGE(TAG, "SPL06 not initialized or calibration not loaded");
        return ESP_ERR_INVALID_STATE;
    }

    if (pressure_pa == NULL || temp_celsius == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Wait for measurement to be ready
    int timeout = 100; // 100 attempts
    while (!spl06_is_ready() && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (timeout <= 0) {
        ESP_LOGE(TAG, "Timeout waiting for measurement");
        return ESP_ERR_TIMEOUT;
    }

    int32_t raw_pressure = spl06_read_raw_pressure();
    int32_t raw_temperature = spl06_read_raw_temperature();

    // Scale factors (for 8x oversampling)
    float pscal = 253952.0f; // 2^20 / 8
    float tscal = 524288.0f; // 2^20

    // Normalized values
    float p_raw_sc = (float)raw_pressure / pscal;
    float t_raw_sc = (float)raw_temperature / tscal;

    // Calculate pressure using calibration coefficients
    float pressure = (float)calib_coeffs.c00 + 
                     p_raw_sc * ((float)calib_coeffs.c10 + p_raw_sc * ((float)calib_coeffs.c20 + p_raw_sc * (float)calib_coeffs.c30)) +
                     t_raw_sc * (float)calib_coeffs.c01 +
                     t_raw_sc * p_raw_sc * ((float)calib_coeffs.c11 + p_raw_sc * (float)calib_coeffs.c21);

    // Calculate temperature using calibration coefficients
    float temperature = (float)calib_coeffs.c0 * 0.5f + (float)calib_coeffs.c1 * t_raw_sc;

    *pressure_pa = pressure;
    *temp_celsius = temperature;

    return ESP_OK;
}

#endif // CONFIG_ENABLE_SPL06

