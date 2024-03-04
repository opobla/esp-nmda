#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"

#define NVS_PARTITION_NAME "nvs_settings"

nvs_handle_t my_nvs_handle; // Declare an NVS handle


int load_settings_from_nvs() {
    esp_err_t err;
    char buffer[64];
    size_t buffer_size = sizeof(buffer);
    
    ESP_LOGI("LOAD_SETTING_FROM_NVS", "Loading settings from NVS");
    err = nvs_flash_init_partition(NVS_PARTITION_NAME);
    if (err != ESP_OK) {
        printf("Error initializing NVS: %s\n", esp_err_to_name(err));
        return err;
    }

    err = nvs_open_from_partition(NVS_PARTITION_NAME, "settings", NVS_READONLY, &my_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("LOAD_SETTING_FROM_NVS", "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }

    return 0;
}

int settings_get_str(const char * key, char * value, size_t* max_length) {
    esp_err_t err;
    const char* TAG = "SETTINGS - SETTINGS_GET_STR";
    
    err = nvs_get_str(my_nvs_handle, key, value, max_length);
    if (err == ESP_OK) {
        // Successfully read the string from NVS
        ESP_LOGD(TAG, "Key %s Value: %s\n", key, value);
    } else {
        ESP_LOGE(TAG, "Error (%s) reading!\n", esp_err_to_name(err));
        return err;
    }
    return 0;
}
