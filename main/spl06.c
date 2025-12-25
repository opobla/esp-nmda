#include "spl06.h"

#ifdef CONFIG_ENABLE_SPL06

#include "i2c_bus.h"
#include "esp32-libs.h"
#include <math.h>
#include <inttypes.h>

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
    // SPL06 uses standard I2C protocol: write register address, then read data
    // Use write_read_repeated_start to guarantee Repeated Start condition
    esp_err_t ret = i2c_bus_write_read_repeated_start(SPL06_I2C_ADDR, &reg, 1, data, len, 1000);
    
    // Debug: log register reads (only for temperature registers when debugging)
    // Removed ESP_LOGD as it's not visible in user's system
    
    return ret;
}

static esp_err_t spl06_write_register(uint8_t reg, uint8_t data)
{
    // SPL06 uses standard I2C protocol: [register_address][data]
    uint8_t buffer[2] = {reg, data};
    return i2c_bus_write(SPL06_I2C_ADDR, buffer, 2, 1000);
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
    ESP_LOGI(TAG, "c0=%d, c1=%d, c00=%" PRId32 ", c10=%" PRId32, 
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
    
    // Small delay to ensure I2C bus is ready
    vTaskDelay(pdMS_TO_TICKS(10));
    
    uint8_t product_id;
    esp_err_t ret;

    // Read product ID to verify sensor presence
    ESP_LOGI(TAG, "Reading product ID from register 0x%02X", SPL06_REG_PRODUCT_ID);
    ret = spl06_read_register(SPL06_REG_PRODUCT_ID, &product_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read product ID: %s (0x%X)", esp_err_to_name(ret), ret);
        ESP_LOGE(TAG, "Check I2C connection and address (0x%02X)", SPL06_I2C_ADDR);
        ESP_LOGE(TAG, "Possible causes:");
        ESP_LOGE(TAG, "  - Sensor not connected or powered");
        ESP_LOGE(TAG, "  - Wrong I2C address (try 0x76 or 0x77)");
        ESP_LOGE(TAG, "  - I2C wiring issue (SDA=GPIO%d, SCL=GPIO%d)", 
                 CONFIG_I2C_MASTER_SDA_IO, CONFIG_I2C_MASTER_SCL_IO);
        ESP_LOGE(TAG, "  - Missing pull-up resistors (should be ~4.7kΩ)");
        ESP_LOGE(TAG, "  - Check I2C bus scan output above for detected devices");
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

    // ========================================================================
    // MAXIMUM BAROMETRIC PRECISION CONFIGURATION
    // ========================================================================
    // Trade-offs for maximum precision:
    //   - Oversampling: 128x (maximum) for both pressure and temperature
    //   - Measurement rate: 2/s (reduced from 4/s due to high oversampling)
    //   - First measurement time: ~500ms (increased from ~250ms)
    //   - Power consumption: Higher due to more internal measurements per result
    //
    // According to SPL06-001 datasheet:
    //   - Higher oversampling = lower noise = higher precision
    //   - Temperature oversampling also important because T is used in P compensation
    //   - With 128x oversampling, maximum practical rate is 2/s
    // ========================================================================

    // Configure pressure measurement: 2 samples per second, 128x oversampling (MAXIMUM)
    // PRS_CFG register (0x06):
    //   Bits [6:4]: PM_RATE = 001 (2 measurements/second)
    //   Bits [3:0]: PM_PRC  = 0111 (128x oversampling - MAXIMUM)
    // Result: 0b00010111 = 0x17
    ret = spl06_write_register(SPL06_REG_PRS_CFG, 0x17);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure pressure");
        return ret;
    }
    ESP_LOGI(TAG, "Pressure configured: Rate=2/s, Oversampling=128x (0x17) - MAXIMUM PRECISION");

    // Configure temperature measurement: 2 samples per second, 128x oversampling (MAXIMUM)
    // TMP_CFG register (0x07):
    //   Bit 7: TMP_EXT = 1 (use external MEMS sensor - REQUIRED for correct calibration!)
    //   Bits [6:4]: TMP_RATE = 001 (2 measurements/second)
    //   Bits [3:0]: TMP_PRC  = 0111 (128x oversampling - MAXIMUM)
    // Result: 0b10010111 = 0x97
    // IMPORTANT: Calibration coefficients are based on external MEMS sensor!
    // Using internal ASIC sensor (TMP_EXT=0) gives incorrect temperature values.
    ret = spl06_write_register(SPL06_REG_TMP_CFG, 0x97);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure temperature");
        return ret;
    }
    ESP_LOGI(TAG, "Temperature configured: Rate=2/s, Oversampling=128x, External MEMS (0x97) - MAXIMUM PRECISION");

    // Configure result bit-shift for high oversampling (REQUIRED when oversampling > 8x)
    // CFG_REG register (0x09):
    //   Bit 2: P_SHIFT = 1 (enable pressure result bit-shift, required for PM_PRC > 8)
    //   Bit 3: T_SHIFT = 1 (enable temperature result bit-shift, required for TMP_PRC > 8)
    // Result: 0b00001100 = 0x0C
    // NOTE: Without this, raw values will be incorrect with oversampling > 8x!
    ret = spl06_write_register(SPL06_REG_CFG_REG, 0x0C);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure result bit-shift");
        return ret;
    }
    ESP_LOGI(TAG, "Result bit-shift enabled for P and T (0x0C) - Required for 128x oversampling");

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
    // With oversampling 128x and rate 2/s, first measurement takes ~500-600ms
    ESP_LOGI(TAG, "Waiting for first measurement (128x oversampling takes longer)...");
    int init_timeout = 100; // 100 attempts = 1000ms (increased for 128x oversampling)
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
    
    // Debug logging removed (ESP_LOGD not visible in user's system)
    
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
    uint8_t data[3];
    
    // Read all 3 bytes in one atomic transaction starting from TMP_B2
    // This ensures we read a coherent measurement and prevents reading bytes
    // from different measurements if the sensor updates between reads.
    // SPL06-001 uses register addresses 0x03 (TMP_B2), 0x04 (TMP_B1), 0x05 (TMP_B0)
    if (spl06_read_register(SPL06_REG_TMP_B2, data, 3) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature registers");
        return 0;
    }

    // 24-bit signed value in two's complement
    // TMP_B2 is MSB (bits 23-16), TMP_B1 is middle (bits 15-8), TMP_B0 is LSB (bits 7-0)
    int32_t raw = ((int32_t)data[0] << 16) | ((int32_t)data[1] << 8) | data[2];
    
    // Sign extend from 24-bit to 32-bit
    if (raw & 0x800000) {
        raw |= 0xFF000000; // Sign extend for negative values
    }

    // Note: With 128x oversampling and MEMS sensor (TMP_EXT=1), typical raw values
    // for room temperature (~20-25°C) are around 500K to 1.5M (depending on calibration).
    // With kT=2088960, a raw value of 800K gives t_raw_sc ≈ 0.383

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
    
    // Validate raw values before processing
    // With 8x oversampling, typical raw ranges are approximately:
    // - Pressure: depends on altitude, typically -8M to +8M
    // - Temperature: approximately -1M to +1M for -40°C to +85°C range
    if (raw_pressure == 0) {
        ESP_LOGE(TAG, "Invalid pressure raw value: 0");
        return ESP_ERR_INVALID_RESPONSE;
    }
    // Temperature raw validation adjusted for 8x oversampling range
    if (raw_temperature == 0) {
        ESP_LOGE(TAG, "Invalid temperature raw value: 0");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Scale factors for 128x oversampling (from SPL06-001 datasheet)
    // kT and kP for oversampling rates: 
    //   single=524288, 2x=1572864, 4x=3670016, 8x=7864320,
    //   16x=253952, 32x=516096, 64x=1040384, 128x=2088960
    float kP = 2088960.0f;  // Pressure scale factor for 128x oversampling
    float kT = 2088960.0f;  // Temperature scale factor for 128x oversampling

    // Normalized values (scaled raw values)
    float p_raw_sc = (float)raw_pressure / kP;
    float t_raw_sc = (float)raw_temperature / kT;

    // Calculate pressure using calibration coefficients (from datasheet)
    // Pcomp = c00 + Praw_sc*(c10 + Praw_sc*(c20 + Praw_sc*c30)) + 
    //         Traw_sc*c01 + Traw_sc*Praw_sc*(c11 + Praw_sc*c21)
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
    
    // Validate raw value before processing
    // With 8x oversampling, temperature raw value of 0 indicates error
    if (raw_temperature == 0) {
        ESP_LOGE(TAG, "Invalid temperature raw value: 0");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Scale factor kT for temperature (128x oversampling - MAXIMUM PRECISION)
    // From SPL06-001 datasheet: kT for 128x oversampling = 2088960
    float kT = 2088960.0f;

    // Normalized value: Traw_sc = Traw / kT
    float t_raw_sc = (float)raw_temperature / kT;

    // Calculate temperature using calibration coefficients
    // Formula from datasheet: T = c0 * 0.5 + c1 * Traw_sc
    float temperature = (float)calib_coeffs.c0 * 0.5f + (float)calib_coeffs.c1 * t_raw_sc;

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
    
    // Validate raw values before processing
    if (raw_pressure == 0) {
        ESP_LOGE(TAG, "Invalid pressure raw value: 0");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (raw_temperature == 0) {
        ESP_LOGE(TAG, "Invalid temperature raw value: 0");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Scale factors for 128x oversampling (from SPL06-001 datasheet)
    // kT and kP for oversampling rates: 
    //   single=524288, 2x=1572864, 4x=3670016, 8x=7864320,
    //   16x=253952, 32x=516096, 64x=1040384, 128x=2088960
    float kP = 2088960.0f;  // Pressure scale factor for 128x oversampling
    float kT = 2088960.0f;  // Temperature scale factor for 128x oversampling

    // Normalized values (scaled raw values)
    float p_raw_sc = (float)raw_pressure / kP;
    float t_raw_sc = (float)raw_temperature / kT;

    // Calculate pressure using calibration coefficients (from datasheet)
    // Pcomp = c00 + Praw_sc*(c10 + Praw_sc*(c20 + Praw_sc*c30)) + 
    //         Traw_sc*c01 + Traw_sc*Praw_sc*(c11 + Praw_sc*c21)
    float pressure = (float)calib_coeffs.c00 + 
                     p_raw_sc * ((float)calib_coeffs.c10 + p_raw_sc * ((float)calib_coeffs.c20 + p_raw_sc * (float)calib_coeffs.c30)) +
                     t_raw_sc * (float)calib_coeffs.c01 +
                     t_raw_sc * p_raw_sc * ((float)calib_coeffs.c11 + p_raw_sc * (float)calib_coeffs.c21);

    // Calculate temperature using calibration coefficients
    // Formula from datasheet: T = c0 * 0.5 + c1 * Traw_sc
    float temperature = (float)calib_coeffs.c0 * 0.5f + (float)calib_coeffs.c1 * t_raw_sc;

    *pressure_pa = pressure;
    *temp_celsius = temperature;

    return ESP_OK;
}

#endif // CONFIG_ENABLE_SPL06

