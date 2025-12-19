#include "common.h"
#include "datastructures.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "mqtt.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#include <stdio.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "settings.h"
#include "wifi.h"
#include "sntp.h"
#include "mqtt.h"

#ifdef CONFIG_ENABLE_I2C_BUS
#include "i2c_bus.h"
#endif

#ifdef CONFIG_ENABLE_SPL06
#include "spl06.h"
#endif

QueueHandle_t telemetry_queue;
SemaphoreHandle_t wifi_semaphore;
SemaphoreHandle_t sntp_semaphore;
SemaphoreHandle_t mqtt_semaphore;
SemaphoreHandle_t dtct_semaphore;

nmda_init_config_t nmda_config = NMDA_INIT_CONFIG_DEFAULT();

void app_main(void)
{
    ESP_LOGI("APP_MAIN", "is running on %d Core", xPortGetCoreID());
    init_nvs();

    ESP_LOGI("APP_MAIN", "Loading nmda configuration");
    ESP_ERROR_CHECK(load_nmda_settings(&nmda_config));
    print_nmda_init_config(&nmda_config);

    telemetry_queue = xQueueCreate(100, sizeof(struct telemetry_message));
    // Create semaphores
    wifi_semaphore = xSemaphoreCreateBinary();
    sntp_semaphore = xSemaphoreCreateBinary();
    mqtt_semaphore = xSemaphoreCreateBinary();

    wifi_setup(&nmda_config);
    ntp_setup(&nmda_config);
    ESP_LOGI("APP_MAIN", "Initializing GPIO...");
    init_GPIO();
    ESP_LOGI("APP_MAIN", "GPIO initialized");

    ESP_LOGI("APP_MAIN", "Checking I2C and SPL06 configuration...");
    ESP_LOGI("APP_MAIN", "CONFIG_ENABLE_I2C_BUS: %s", 
#ifdef CONFIG_ENABLE_I2C_BUS
             "ENABLED"
#else
             "DISABLED"
#endif
    );
    ESP_LOGI("APP_MAIN", "CONFIG_ENABLE_SPL06: %s", 
#ifdef CONFIG_ENABLE_SPL06
             "ENABLED"
#else
             "DISABLED"
#endif
    );

#ifdef CONFIG_ENABLE_I2C_BUS
    ESP_LOGI("APP_MAIN", "Initializing I2C bus");
    esp_err_t i2c_ret = i2c_bus_init();
    if (i2c_ret == ESP_OK) {
        ESP_LOGI("APP_MAIN", "I2C bus initialized successfully");
        // Scan I2C bus to see what devices are connected
        i2c_bus_scan();
    } else {
        ESP_LOGE("APP_MAIN", "I2C bus initialization failed: %s", esp_err_to_name(i2c_ret));
    }
#else
    ESP_LOGW("APP_MAIN", "I2C bus support is disabled in configuration");
#endif

#ifdef CONFIG_ENABLE_SPL06
    ESP_LOGI("APP_MAIN", "Initializing SPL06 sensor");
    esp_err_t spl06_ret = spl06_init();
    if (spl06_ret == ESP_OK) {
        ESP_LOGI("APP_MAIN", "SPL06 sensor initialized successfully");
        BaseType_t task_ret = xTaskCreatePinnedToCore(&spl06_monitor_task, "SPL06 Monitor", 4096, NULL, 3, NULL, 1);
        if (task_ret == pdPASS) {
            ESP_LOGI("APP_MAIN", "SPL06 monitor task created successfully");
        } else {
            ESP_LOGE("APP_MAIN", "Failed to create SPL06 monitor task");
        }
    } else {
        ESP_LOGE("APP_MAIN", "SPL06 initialization failed: %s", esp_err_to_name(spl06_ret));
    }
#else
    ESP_LOGW("APP_MAIN", "SPL06 support is disabled in configuration");
#endif

    // MQTT temporarily disabled - uncomment when MQTT server is configured correctly
    // xTaskCreatePinnedToCore(&mss_sender, "Send message", 1024 * 6, &nmda_config, 5, NULL, 0);
    xTaskCreatePinnedToCore(&task_pcnt, "Pulse counter", 1024 * 3, NULL, 1, NULL, 1);

    // xTaskCreatePinnedToCore(&task_ota, "OTA handling", 1024 * 8, NULL, 5, NULL, 0);
}
