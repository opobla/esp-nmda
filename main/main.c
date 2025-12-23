#include "common.h"
#include "datastructures.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
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

QueueHandle_t telemetry_queue;
SemaphoreHandle_t wifi_semaphore;
SemaphoreHandle_t sntp_semaphore;
SemaphoreHandle_t mqtt_semaphore;
SemaphoreHandle_t dtct_semaphore;

nmda_init_config_t nmda_config = NMDA_INIT_CONFIG_DEFAULT();

void app_main(void)
{
    ESP_LOGI("APP_MAIN", "is running on %d Core", xPortGetCoreID());
    
    // Initialize User LED first to indicate system booting (before any other initialization)
#ifdef CONFIG_ENABLE_USER_LED
    ESP_LOGI("APP_MAIN", "Initializing User LED...");
    esp_err_t user_led_ret = user_led_init();
    if (user_led_ret == ESP_OK) {
        user_led_set_condition(USER_LED_BOOTING);
        ESP_LOGI("APP_MAIN", "User LED initialized and showing BOOTING pattern");
    } else {
        ESP_LOGW("APP_MAIN", "Failed to initialize User LED: %s", esp_err_to_name(user_led_ret));
    }
#endif

    init_nvs();

    ESP_LOGI("APP_MAIN", "Loading nmda configuration");
    esp_err_t settings_ret = load_nmda_settings(&nmda_config);
    if (settings_ret != ESP_OK) {
        ESP_LOGW("APP_MAIN", "Failed to load settings (ret: %s), using defaults", esp_err_to_name(settings_ret));
    }
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

#ifdef CONFIG_ENABLE_HV_SUPPORT
    // Temporary: Enable DEBUG logging for HV_ADC to diagnose conversion issues
    esp_log_level_set("HV_ADC", ESP_LOG_DEBUG);
    ESP_LOGI("APP_MAIN", "DEBUG logging enabled for HV_ADC (temporary)");
#endif

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
        // DISABLED: I2C bus scan disabled
        // i2c_bus_scan();
        
#ifdef CONFIG_ENABLE_HV_SUPPORT
        // Verify ADC is present on I2C bus
        // ADS112C04 uses command-based protocol, so we need to send RREG command first
        ESP_LOGI("APP_MAIN", "Checking for HV ADC (ADS112C04) at address 0x%02X...", HV_ADC_I2C_ADDR_DEFAULT);
        uint8_t rreg_cmd = 0x20;  // RREG command for register 0 (CONFIG0)
        uint8_t test_data;
        esp_err_t adc_check = i2c_bus_write_read_repeated_start(HV_ADC_I2C_ADDR_DEFAULT, &rreg_cmd, 1, &test_data, 1, 100);
        if (adc_check == ESP_OK) {
            ESP_LOGI("APP_MAIN", "HV ADC detected! CONFIG0 register read: 0x%02X", test_data);
        } else {
            ESP_LOGW("APP_MAIN", "HV ADC not detected at address 0x%02X: %s", HV_ADC_I2C_ADDR_DEFAULT, esp_err_to_name(adc_check));
            ESP_LOGW("APP_MAIN", "Check I2C connections and verify ADC is powered");
        }
#endif
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

#ifdef CONFIG_ENABLE_HV_SUPPORT
    ESP_LOGI("APP_MAIN", "Initializing HV ADC");
    esp_err_t hv_adc_ret = hv_adc_init();
    if (hv_adc_ret == ESP_OK) {
        ESP_LOGI("APP_MAIN", "HV ADC initialized successfully");
        
        // Create monitor task for periodic ADC readings
        BaseType_t task_ret = xTaskCreatePinnedToCore(&hv_adc_monitor_task, "HV ADC Monitor", 4096, NULL, 3, NULL, 1);
        if (task_ret == pdPASS) {
            ESP_LOGI("APP_MAIN", "HV ADC monitor task created successfully");
        } else {
            ESP_LOGE("APP_MAIN", "Failed to create HV ADC monitor task");
        }
    } else {
        ESP_LOGE("APP_MAIN", "HV ADC initialization failed: %s", esp_err_to_name(hv_adc_ret));
    }
#else
    ESP_LOGW("APP_MAIN", "HV support is disabled in configuration");
#endif

    // MQTT temporarily disabled - uncomment when MQTT server is configured correctly
    // xTaskCreatePinnedToCore(&mss_sender, "Send message", 1024 * 6, &nmda_config, 5, NULL, 0);
    xTaskCreatePinnedToCore(&task_pcnt, "Pulse counter", 1024 * 3, NULL, 1, NULL, 1);

    // xTaskCreatePinnedToCore(&task_ota, "OTA handling", 1024 * 8, NULL, 5, NULL, 0);
}
