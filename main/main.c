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
    init_GPIO();

    xTaskCreatePinnedToCore(&mss_sender, "Send message", 1024 * 6, &nmda_config, 5, NULL, 0);
    xTaskCreatePinnedToCore(&task_pcnt, "Pulse counter", 1024 * 3, NULL, 1, NULL, 1);

    // xTaskCreatePinnedToCore(&task_ota, "OTA handling", 1024 * 8, NULL, 5, NULL, 0);
}
