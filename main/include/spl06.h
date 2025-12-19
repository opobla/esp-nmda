#ifndef __SPL06_H_
#define __SPL06_H_

#include <stdbool.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef CONFIG_ENABLE_SPL06

// SPL06-001 Register addresses
#define SPL06_REG_PSR_B2          0x00
#define SPL06_REG_PSR_B1          0x01
#define SPL06_REG_PSR_B0          0x02
#define SPL06_REG_TMP_B2          0x03
#define SPL06_REG_TMP_B1          0x04
#define SPL06_REG_TMP_B0          0x05
#define SPL06_REG_PRS_CFG         0x06
#define SPL06_REG_TMP_CFG         0x07
#define SPL06_REG_MEAS_CFG        0x08
#define SPL06_REG_CFG_REG         0x09
#define SPL06_REG_INT_STS         0x0A
#define SPL06_REG_FIFO_STS        0x0B
#define SPL06_REG_RESET           0x0C
#define SPL06_REG_PRODUCT_ID      0x0D
#define SPL06_REG_COEF            0x10

// Product ID value
#define SPL06_PRODUCT_ID          0x10

// Measurement configuration
#define SPL06_MEAS_CFG_TMP_RDY    (1 << 7)
#define SPL06_MEAS_CFG_PRS_RDY     (1 << 6)
#define SPL06_MEAS_CFG_TMP_COEF_RDY (1 << 5)
#define SPL06_MEAS_CFG_SENSOR_RDY  (1 << 4)
#define SPL06_MEAS_CFG_MEAS_CTRL_MASK 0x07

// Pressure configuration
#define SPL06_PRS_CFG_RATE_MASK   0x70
#define SPL06_PRS_CFG_PRC_MASK    0x0F

// Temperature configuration
#define SPL06_TMP_CFG_RATE_MASK   0x70
#define SPL06_TMP_CFG_PRC_MASK    0x0F
#define SPL06_TMP_CFG_EXT_MODE    (1 << 7)

/**
 * @brief Initialize the SPL06-001 pressure sensor
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t spl06_init(void);

/**
 * @brief Read pressure from SPL06 sensor
 * 
 * @param pressure_pa Pointer to store pressure in Pascals
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t spl06_read_pressure(float *pressure_pa);

/**
 * @brief Read temperature from SPL06 sensor
 * 
 * @param temp_celsius Pointer to store temperature in Celsius
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t spl06_read_temperature(float *temp_celsius);

/**
 * @brief Read both pressure and temperature from SPL06 sensor
 * 
 * @param pressure_pa Pointer to store pressure in Pascals
 * @param temp_celsius Pointer to store temperature in Celsius
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t spl06_read_both(float *pressure_pa, float *temp_celsius);

/**
 * @brief Check if SPL06 sensor is ready
 * 
 * @return true if sensor is ready, false otherwise
 */
bool spl06_is_ready(void);

#endif // CONFIG_ENABLE_SPL06

#endif // __SPL06_H_

