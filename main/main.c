#include "common.h"
#include "datastructures.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "mqtt.h"
#include "sdkconfig.h"
#include "settings.h"
#include "driver/gpio.h"
#include "meteo_bmp280.h"

#include <stdio.h>
#include "esp_system.h"
#include "esp_partition.h"

QueueHandle_t telemetry_queue;
SemaphoreHandle_t wifi_semaphore;
SemaphoreHandle_t sntp_semaphore;
SemaphoreHandle_t mqtt_semaphore;
SemaphoreHandle_t dtct_semaphore;

void app_main2(void) {
    ESP_LOGI("APP_MAIN", "is running on %d Core", xPortGetCoreID());
    gpio_reset_pin(32);
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL<<32),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = 0,
        .pull_down_en = 0
    };
    gpio_config(&io_conf);

    gpio_set_level(32, 0);
    bmp280_task(NULL);

}
void app_main(void)
{
    telemetry_queue = xQueueCreate(100, sizeof(struct telemetry_message));

    ESP_LOGI("APP_MAIN", "is running on %d Core", xPortGetCoreID());

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGE("LOAD_SETTING_FROM_NVS", "Error (%s) initializing NVS!\n", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    load_settings_from_nvs();

    bmp280_task(NULL);


    // Create semaphores
    wifi_semaphore = xSemaphoreCreateBinary();
    sntp_semaphore = xSemaphoreCreateBinary();
    mqtt_semaphore = xSemaphoreCreateBinary();

    wifi_setup();
    sntp_setup();
    init_GPIO();

    gpio_reset_pin(32);
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL<<32),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = 0,
        .pull_down_en = 0
    };
    gpio_config(&io_conf);

    gpio_set_level(32, 0);


    xTaskCreatePinnedToCore(&mss_sender, "Send message", 1024 * 3, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(&task_pcnt, "Pulse counter", 1024 * 3, NULL, 1, NULL, 1);
    //xTaskCreate(&bmp280_task, "bmp280_task", 4096, NULL, 5, NULL);


    //xTaskCreatePinnedToCore(&task_meteo, "Meteo data handling", 1024 * 3, NULL, 5, NULL, 0);
    // xTaskCreatePinnedToCore(&task_ota, "OTA handling", 1024 * 8, NULL, 5, NULL, 0);
}
