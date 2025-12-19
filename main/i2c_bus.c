#include "i2c_bus.h"

#ifdef CONFIG_ENABLE_I2C_BUS

#include <stdlib.h>
#include <string.h>
#include "esp32-libs.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "I2C_BUS";

// I2C bus configuration
#define I2C_MASTER_SCL_IO           CONFIG_I2C_MASTER_SCL_IO
#define I2C_MASTER_SDA_IO           CONFIG_I2C_MASTER_SDA_IO
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_TIMEOUT_MS       1000   // Default timeout

// Static variables
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static SemaphoreHandle_t i2c_mutex = NULL;
static bool i2c_initialized = false;

esp_err_t i2c_bus_init(void)
{
    if (i2c_initialized) {
        ESP_LOGW(TAG, "I2C bus already initialized");
        return ESP_OK;
    }

    // Create mutex for thread-safe access
    if (i2c_mutex == NULL) {
        i2c_mutex = xSemaphoreCreateMutex();
        if (i2c_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create I2C mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Configure I2C master bus
    i2c_master_bus_config_t i2c_mst_cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&i2c_mst_cfg, &i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_initialized = true;
    ESP_LOGI(TAG, "I2C bus initialized successfully (SDA: GPIO%d, SCL: GPIO%d, Default Speed: %d Hz)",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, CONFIG_I2C_BUS_SPEED);

    return ESP_OK;
}

esp_err_t i2c_bus_deinit(void)
{
    if (!i2c_initialized) {
        return ESP_OK;
    }

    if (i2c_bus_handle != NULL) {
        esp_err_t ret = i2c_del_master_bus(i2c_bus_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete I2C master bus: %s", esp_err_to_name(ret));
            return ret;
        }
        i2c_bus_handle = NULL;
    }

    if (i2c_mutex != NULL) {
        vSemaphoreDelete(i2c_mutex);
        i2c_mutex = NULL;
    }

    i2c_initialized = false;
    ESP_LOGI(TAG, "I2C bus deinitialized");
    return ESP_OK;
}

esp_err_t i2c_bus_write(uint8_t device_addr, const uint8_t *reg_addr, size_t reg_addr_len,
                        const uint8_t *data, size_t data_len, int timeout_ms)
{
    if (!i2c_initialized || i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take I2C mutex");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    // Create I2C device handle for this transaction
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr,
        .scl_speed_hz = CONFIG_I2C_BUS_SPEED,
    };

    i2c_master_dev_handle_t dev_handle;
    ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        xSemaphoreGive(i2c_mutex);
        return ret;
    }

    // Prepare write data: register address (if provided) + data
    size_t total_len = reg_addr_len + data_len;
    uint8_t *write_buffer = malloc(total_len);
    if (write_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate write buffer");
        i2c_master_bus_rm_device(dev_handle);
        xSemaphoreGive(i2c_mutex);
        return ESP_ERR_NO_MEM;
    }

    if (reg_addr_len > 0 && reg_addr != NULL) {
        memcpy(write_buffer, reg_addr, reg_addr_len);
    }
    if (data_len > 0 && data != NULL) {
        memcpy(write_buffer + reg_addr_len, data, data_len);
    }

    // Perform write transaction
    ret = i2c_master_transmit(dev_handle, write_buffer, total_len, timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
    }

    free(write_buffer);
    i2c_master_bus_rm_device(dev_handle);
    xSemaphoreGive(i2c_mutex);

    return ret;
}

esp_err_t i2c_bus_read(uint8_t device_addr, const uint8_t *reg_addr, size_t reg_addr_len,
                       uint8_t *data, size_t data_len, int timeout_ms)
{
    if (!i2c_initialized || i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || data_len == 0) {
        ESP_LOGE(TAG, "Invalid read parameters");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take I2C mutex");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    // Create I2C device handle for this transaction
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr,
        .scl_speed_hz = CONFIG_I2C_BUS_SPEED,
    };

    i2c_master_dev_handle_t dev_handle;
    ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        xSemaphoreGive(i2c_mutex);
        return ret;
    }

    // If register address is provided, write it first
    if (reg_addr_len > 0 && reg_addr != NULL) {
        ret = i2c_master_transmit(dev_handle, (uint8_t *)reg_addr, reg_addr_len, timeout_ms);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C register address write failed: %s", esp_err_to_name(ret));
            i2c_master_bus_rm_device(dev_handle);
            xSemaphoreGive(i2c_mutex);
            return ret;
        }
    }

    // Perform read transaction
    ret = i2c_master_receive(dev_handle, data, data_len, timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
    }

    i2c_master_bus_rm_device(dev_handle);
    xSemaphoreGive(i2c_mutex);

    return ret;
}

esp_err_t i2c_bus_write_read(uint8_t device_addr, const uint8_t *write_data, size_t write_len,
                              uint8_t *read_data, size_t read_len, int timeout_ms)
{
    if (!i2c_initialized || i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (write_data == NULL || write_len == 0 || read_data == NULL || read_len == 0) {
        ESP_LOGE(TAG, "Invalid write_read parameters");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take I2C mutex");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    // Create I2C device handle for this transaction
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr,
        .scl_speed_hz = CONFIG_I2C_BUS_SPEED,
    };

    i2c_master_dev_handle_t dev_handle;
    ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        xSemaphoreGive(i2c_mutex);
        return ret;
    }

    // Write then read in separate transactions
    ret = i2c_master_transmit(dev_handle, (uint8_t *)write_data, write_len, timeout_ms);
    if (ret == ESP_OK) {
        ret = i2c_master_receive(dev_handle, read_data, read_len, timeout_ms);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C read after write failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "I2C write before read failed: %s", esp_err_to_name(ret));
    }

    i2c_master_bus_rm_device(dev_handle);
    xSemaphoreGive(i2c_mutex);

    return ret;
}

esp_err_t i2c_bus_scan(void)
{
    if (!i2c_initialized || i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Scanning I2C bus for devices...");
    int found_count = 0;

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = CONFIG_I2C_BUS_SPEED,
        };

        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &dev_handle);
        if (ret != ESP_OK) {
            continue;
        }

        // Try to write a dummy byte (some devices need this to respond)
        uint8_t dummy_write = 0x00;
        ret = i2c_master_transmit(dev_handle, &dummy_write, 1, 50);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found device at address 0x%02X (responded to write)", addr);
            found_count++;
        } else {
            // Some devices only respond to read, try that
            uint8_t dummy_read;
            ret = i2c_master_receive(dev_handle, &dummy_read, 1, 50);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Found device at address 0x%02X (responded to read)", addr);
                found_count++;
            }
        }

        i2c_master_bus_rm_device(dev_handle);
    }

    if (found_count == 0) {
        ESP_LOGW(TAG, "No I2C devices found on bus");
        ESP_LOGW(TAG, "Check connections: SDA=GPIO%d, SCL=GPIO%d", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    } else {
        ESP_LOGI(TAG, "Found %d device(s) on I2C bus", found_count);
    }

    return ESP_OK;
}

#endif // CONFIG_ENABLE_I2C_BUS

