#include "i2c_bus.h"

#ifdef CONFIG_ENABLE_I2C_BUS

#include <stdlib.h>
#include <string.h>
#include "esp32-libs.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#ifdef CONFIG_ENABLE_HV_SUPPORT
#include "hv_adc.h"  // For HV_ADC_I2C_ADDR_DEFAULT
#endif

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

// Persistent device handles (created once, reused for all transactions)
// This eliminates overhead of creating/deleting handles and may help with Repeated Start
static i2c_master_dev_handle_t i2c_adc_handle = NULL;      // ADS112C04 at HV_ADC_I2C_ADDR_DEFAULT
#ifdef CONFIG_ENABLE_SPL06
static i2c_master_dev_handle_t i2c_spl06_handle = NULL;    // SPL06 at CONFIG_SPL06_I2C_ADDRESS
#endif

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

    // Create persistent device handles for known I2C devices
    // These handles are created once and reused for all transactions, eliminating
    // overhead and potentially improving Repeated Start behavior
    
#ifdef CONFIG_ENABLE_HV_SUPPORT
    // Create handle for ADS112C04 ADC
    i2c_device_config_t adc_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = HV_ADC_I2C_ADDR_DEFAULT,
        .scl_speed_hz = CONFIG_I2C_BUS_SPEED,
    };
    ret = i2c_master_bus_add_device(i2c_bus_handle, &adc_cfg, &i2c_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create persistent handle for ADC (0x%02X): %s", 
                 HV_ADC_I2C_ADDR_DEFAULT, esp_err_to_name(ret));
        // Non-fatal: handle will be created on-demand if needed
    } else {
        ESP_LOGI(TAG, "Created persistent handle for ADC (0x%02X)", HV_ADC_I2C_ADDR_DEFAULT);
    }
#endif

#ifdef CONFIG_ENABLE_SPL06
    // Create handle for SPL06 sensor
    i2c_device_config_t spl06_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_SPL06_I2C_ADDRESS,
        .scl_speed_hz = CONFIG_I2C_BUS_SPEED,
    };
    ret = i2c_master_bus_add_device(i2c_bus_handle, &spl06_cfg, &i2c_spl06_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create persistent handle for SPL06 (0x%02X): %s", 
                 CONFIG_SPL06_I2C_ADDRESS, esp_err_to_name(ret));
        // Non-fatal: handle will be created on-demand if needed
    } else {
        ESP_LOGI(TAG, "Created persistent handle for SPL06 (0x%02X)", CONFIG_SPL06_I2C_ADDRESS);
    }
#endif

    i2c_initialized = true;
    ESP_LOGI(TAG, "I2C bus initialized successfully (SDA: GPIO%d, SCL: GPIO%d, Speed: %d Hz)",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, CONFIG_I2C_BUS_SPEED);

    return ESP_OK;
}

esp_err_t i2c_bus_deinit(void)
{
    if (!i2c_initialized) {
        return ESP_OK;
    }

    // Remove persistent device handles (though system never terminates, this is for completeness)
#ifdef CONFIG_ENABLE_HV_SUPPORT
    if (i2c_adc_handle != NULL) {
        i2c_master_bus_rm_device(i2c_adc_handle);
        i2c_adc_handle = NULL;
    }
#endif

#ifdef CONFIG_ENABLE_SPL06
    if (i2c_spl06_handle != NULL) {
        i2c_master_bus_rm_device(i2c_spl06_handle);
        i2c_spl06_handle = NULL;
    }
#endif

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

// Helper function to get persistent device handle or create on-demand
static i2c_master_dev_handle_t i2c_get_device_handle(uint8_t device_addr)
{
    // Check if we have a persistent handle for this device
#ifdef CONFIG_ENABLE_HV_SUPPORT
    if (device_addr == HV_ADC_I2C_ADDR_DEFAULT && i2c_adc_handle != NULL) {
        return i2c_adc_handle;
    }
#endif

#ifdef CONFIG_ENABLE_SPL06
    if (device_addr == CONFIG_SPL06_I2C_ADDRESS && i2c_spl06_handle != NULL) {
        return i2c_spl06_handle;
    }
#endif

    // No persistent handle found, create one on-demand (fallback)
    // This should rarely happen if initialization was successful
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr,
        .scl_speed_hz = CONFIG_I2C_BUS_SPEED,
    };

    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create device handle for 0x%02X: %s", device_addr, esp_err_to_name(ret));
        return NULL;
    }
    
    ESP_LOGW(TAG, "Created on-demand handle for device 0x%02X (should use persistent handle)", device_addr);
    return dev_handle;
}

esp_err_t i2c_bus_write(uint8_t device_addr, const uint8_t *data, size_t data_len, int timeout_ms)
{
    if (!i2c_initialized || i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || data_len == 0) {
        ESP_LOGE(TAG, "Invalid write parameters");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take I2C mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Get persistent device handle (or create on-demand if not available)
    i2c_master_dev_handle_t dev_handle = i2c_get_device_handle(device_addr);
    if (dev_handle == NULL) {
        xSemaphoreGive(i2c_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    // Perform write transaction using persistent handle
    // Note: Each device is responsible for constructing the complete message
    // (e.g., SPL06: [reg_addr][data], ADS112C04: [command][data])
    esp_err_t ret = i2c_master_transmit(dev_handle, (uint8_t *)data, data_len, timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
    }

    xSemaphoreGive(i2c_mutex);

    return ret;
}

esp_err_t i2c_bus_read(uint8_t device_addr, uint8_t *data, size_t data_len, int timeout_ms)
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

    // Get persistent device handle (or create on-demand if not available)
    i2c_master_dev_handle_t dev_handle = i2c_get_device_handle(device_addr);
    if (dev_handle == NULL) {
        xSemaphoreGive(i2c_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    // Perform read transaction using persistent handle
    // Note: If device requires writing register address first, use i2c_bus_write_read_repeated_start()
    esp_err_t ret = i2c_master_receive(dev_handle, data, data_len, timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
    }

    xSemaphoreGive(i2c_mutex);

    return ret;
}

esp_err_t i2c_bus_write_read_repeated_start(uint8_t device_addr, const uint8_t *write_data, size_t write_len,
                                             uint8_t *read_data, size_t read_len, int timeout_ms)
{
    if (!i2c_initialized || i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (write_data == NULL || write_len == 0 || read_data == NULL || read_len == 0) {
        ESP_LOGE(TAG, "Invalid write_read_repeated_start parameters");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take I2C mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Get persistent device handle (or create on-demand if not available)
    i2c_master_dev_handle_t dev_handle = i2c_get_device_handle(device_addr);
    if (dev_handle == NULL) {
        xSemaphoreGive(i2c_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    // Use i2c_master_transmit_receive() which guarantees Repeated Start
    // According to ESP-IDF documentation:
    // https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2c.html
    // This function is designed specifically for write + read transactions
    // and guarantees Repeated Start automatically, which is critical for
    // devices like ADS112C04 that discard commands if STOP is sent instead.
    esp_err_t ret = i2c_master_transmit_receive(dev_handle,
                                                (uint8_t *)write_data, write_len,
                                                read_data, read_len,
                                                timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C transmit_receive failed: %s", esp_err_to_name(ret));
    }

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

    if (found_count > 0) {
        ESP_LOGI(TAG, "Found %d device(s) on I2C bus", found_count);
    }

    return ESP_OK;
}

#endif // CONFIG_ENABLE_I2C_BUS

