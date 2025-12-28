#include "common.h"
#include "datastructures.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "settings.h"
#include "wifi.h"
#include "sntp.h"
#include "mqtt.h"

#ifdef CONFIG_ENABLE_USER_LED
#include "user_led.h"
#endif

#ifdef CONFIG_ENABLE_I2C_BUS
#include "i2c_bus.h"
#endif

#ifdef CONFIG_ENABLE_SPL06
#include "spl06.h"
#endif

#ifdef CONFIG_ENABLE_HV_SUPPORT
#include "hv_adc.h"
#endif

#ifdef CONFIG_ENABLE_RMT_PULSE_DETECTION
#include "rmt_pulse_capture.h"
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
    
    // Initialize User LED first to indicate system booting
#ifdef CONFIG_ENABLE_USER_LED
    esp_err_t user_led_ret = user_led_init();
    if (user_led_ret == ESP_OK) {
        user_led_set_condition(USER_LED_BOOTING);
    }
#endif

    init_nvs();

    // Load configuration
    esp_err_t settings_ret = load_nmda_settings(&nmda_config);
    if (settings_ret != ESP_OK) {
        ESP_LOGW("APP_MAIN", "Failed to load settings, using defaults");
    }

    // Create telemetry queue and semaphores
    // Note: struct telemetry_message now uses dynamic allocation for RMT pulse arrays
    // Size: base structure + pointer (8 bytes) = ~25 bytes per message
    // The actual pulse arrays are allocated separately and freed after processing
    telemetry_queue = xQueueCreate(100, sizeof(struct telemetry_message));
    if (telemetry_queue == NULL) {
        ESP_LOGE("APP_MAIN", "Failed to create telemetry queue - insufficient memory!");
        ESP_LOGE("APP_MAIN", "Required size: %zu bytes per message, %zu bytes total", 
                 sizeof(struct telemetry_message), 
                 100 * sizeof(struct telemetry_message));
        esp_restart();
    } else {
        ESP_LOGI("APP_MAIN", "Telemetry queue created: %zu bytes per message, %zu bytes total", 
                 sizeof(struct telemetry_message), 
                 100 * sizeof(struct telemetry_message));
    }
    wifi_semaphore = xSemaphoreCreateBinary();
    sntp_semaphore = xSemaphoreCreateBinary();
    mqtt_semaphore = xSemaphoreCreateBinary();

    // Initialize WiFi and NTP
    wifi_setup(&nmda_config);
    ntp_setup(&nmda_config);

    // Wait for WiFi and NTP to be ready
    // Without WiFi/NTP, data collection is meaningless - reset the system
    if (xSemaphoreTake(wifi_semaphore, pdMS_TO_TICKS(30000)) != pdTRUE) {
        ESP_LOGE("APP_MAIN", "WiFi connection timeout - resetting system");
        esp_restart();
    }
    if (xSemaphoreTake(sntp_semaphore, pdMS_TO_TICKS(30000)) != pdTRUE) {
        ESP_LOGE("APP_MAIN", "NTP synchronization timeout - resetting system");
        esp_restart();
    }

    // Initialize GPIO (required for PCNT, optional for interrupt detection)
    init_GPIO();

#ifdef CONFIG_ENABLE_I2C_BUS
    esp_err_t i2c_ret = i2c_bus_init();
    if (i2c_ret != ESP_OK) {
        ESP_LOGE("APP_MAIN", "I2C bus initialization failed: %s", esp_err_to_name(i2c_ret));
    } else {
        vTaskDelay(pdMS_TO_TICKS(50));  // Allow I2C bus to stabilize
    }
#endif

#ifdef CONFIG_ENABLE_SPL06
    esp_err_t spl06_ret = spl06_init();
    if (spl06_ret == ESP_OK) {
        BaseType_t task_ret = xTaskCreatePinnedToCore(&spl06_monitor_task, "SPL06 Monitor", 4096, NULL, 3, NULL, 1);
        if (task_ret != pdPASS) {
            ESP_LOGE("APP_MAIN", "Failed to create SPL06 monitor task");
        }
    } else {
        ESP_LOGE("APP_MAIN", "SPL06 initialization failed: %s", esp_err_to_name(spl06_ret));
    }
#endif

#ifdef CONFIG_ENABLE_HV_SUPPORT
    esp_err_t hv_adc_ret = hv_adc_init();
    if (hv_adc_ret == ESP_OK) {
        BaseType_t task_ret = xTaskCreatePinnedToCore(&hv_adc_monitor_task, "HV ADC Monitor", 4096, NULL, 3, NULL, 1);
        if (task_ret != pdPASS) {
            ESP_LOGE("APP_MAIN", "Failed to create HV ADC monitor task");
        }
    } else {
        ESP_LOGE("APP_MAIN", "HV ADC initialization failed: %s", esp_err_to_name(hv_adc_ret));
    }
#endif

    // Start MQTT sender and other tasks
    xTaskCreatePinnedToCore(&mss_sender, "Send message", 1024 * 6, &nmda_config, 5, NULL, 0);
    xTaskCreatePinnedToCore(&task_pcnt, "Pulse counter", 1024 * 8, NULL, 1, NULL, 1);

#ifdef CONFIG_ENABLE_RMT_PULSE_DETECTION
    // Initialize RMT pulse capture
    esp_err_t rmt_ret = rmt_pulse_capture_init();
    if (rmt_ret == ESP_OK) {
        BaseType_t task_ret = xTaskCreatePinnedToCore(&task_rmt_event_processor, "RMT Event Processor", 4096, NULL, 3, NULL, 1);
        if (task_ret != pdPASS) {
            ESP_LOGE("APP_MAIN", "Failed to create RMT event processor task");
        } else {
            ESP_LOGI("APP_MAIN", "RMT pulse capture initialized and task created");
        }
    } else {
        ESP_LOGE("APP_MAIN", "RMT pulse capture initialization failed: %s", esp_err_to_name(rmt_ret));
    }
#endif
}
