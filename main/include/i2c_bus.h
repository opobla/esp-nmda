#ifndef __I2C_BUS_H_
#define __I2C_BUS_H_

#include "esp_err.h"
#include "sdkconfig.h"

#ifdef CONFIG_ENABLE_I2C_BUS

/**
 * @brief Initialize the I2C bus
 * 
 * Initializes the I2C master driver on GPIO21 (SDA) and GPIO22 (SCL).
 * The bus is configured as a shared resource with thread-safe access.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_bus_init(void);

/**
 * @brief Deinitialize the I2C bus
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_bus_deinit(void);

/**
 * @brief Write data to an I2C device
 * 
 * Each device is responsible for constructing the complete message buffer.
 * For example:
 *   - SPL06: buffer = [reg_addr][data]
 *   - ADS112C04: buffer = [command][data] (command already includes register address)
 * 
 * @param device_addr 7-bit I2C device address
 * @param data Complete message data to write (including register address/command if needed)
 * @param data_len Length of data in bytes
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_bus_write(uint8_t device_addr, const uint8_t *data, size_t data_len, int timeout_ms);

/**
 * @brief Read data from an I2C device
 * 
 * This function performs a simple read operation. If the device requires
 * writing a register address first, use i2c_bus_write_read_repeated_start() instead.
 * 
 * @param device_addr 7-bit I2C device address
 * @param data Buffer to store read data
 * @param data_len Number of bytes to read
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_bus_read(uint8_t device_addr, uint8_t *data, size_t data_len, int timeout_ms);

/**
 * @brief Write then read with explicit Repeated Start (for devices that require it)
 * 
 * This function uses the low-level I2C API to guarantee a Repeated Start condition
 * between the write and read phases. This is critical for devices like ADS112C04
 * that discard commands if a STOP is sent instead of a Repeated Start.
 * 
 * @param device_addr I2C device address (7-bit)
 * @param write_data Data to write
 * @param write_len Length of write data
 * @param read_data Buffer to store read data
 * @param read_len Length of read data
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_write_read_repeated_start(uint8_t device_addr, const uint8_t *write_data, size_t write_len,
                                             uint8_t *read_data, size_t read_len, int timeout_ms);

/**
 * @brief Scan I2C bus for devices
 * 
 * Scans the I2C bus and prints addresses of devices that respond.
 * Useful for debugging I2C connection issues.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_bus_scan(void);

#endif // CONFIG_ENABLE_I2C_BUS

#endif // __I2C_BUS_H_

