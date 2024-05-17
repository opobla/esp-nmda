#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"
#include "settings.h"
#include "ini.h"
#include "string.h"
#include "sdcard.h"

#define TAG "SETTINGS"
#define NVS_PARTITION_NAME "nvs_settings"

nvs_handle_t my_nvs_handle; // Declare an NVS handle

// This function is called for every configuration item found in the ini file
int nmda_init_handler(void* config_struct, const char* section, const char* name, const char* value) {
    nmda_init_config_t* pconfig = (nmda_init_config_t*)config_struct;
    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("wifi", "wifi_essid")) {
        pconfig->wifi_essid = strdup(value);
    } else if (MATCH("wifi", "wifi_password")) {
        pconfig->wifi_password = strdup(value);
    } else if (MATCH("wifi", "wifi_ntp_server")) {
        pconfig->wifi_ntp_server = strdup(value);
    } else if (MATCH("mqtt", "mqtt_server")) {
        pconfig->mqtt_server = strdup(value);
    } else if (MATCH("mqtt", "mqtt_port")) {
        pconfig->mqtt_port = strdup(value);
    } else if (MATCH("mqtt", "mqtt_user")) {
        pconfig->mqtt_user = strdup(value);
    } else if (MATCH("mqtt", "mqtt_password")) {
        pconfig->mqtt_password = strdup(value);
    } else if (MATCH("mqtt", "mqtt_transport")) {
        pconfig->mqtt_transport = strdup(value);
    } else if (MATCH("mqtt", "mqtt_ca_cert")) {
        if (pconfig->mqtt_ca_cert != NULL) {
            char* dest = (char*)malloc(strlen(value) + strlen(pconfig->mqtt_ca_cert) + 2);
            strcpy(dest, pconfig->mqtt_ca_cert);
            strcat(dest, "\n");
            free(pconfig->mqtt_ca_cert);
            strcat(dest, value);
            pconfig->mqtt_ca_cert = dest;
        } else {
            pconfig->mqtt_ca_cert = strdup(value);
        }
    } else if (MATCH("mqtt", "mqtt_device_id")) {
        pconfig->mqtt_device_id = strdup(value);
    } else if (MATCH("mqtt", "mqtt_experiment")) {
        pconfig->mqtt_experiment = strdup(value);
    } else if (MATCH("mqtt", "mqtt_station")) {
        pconfig->mqtt_station = strdup(value);
    } else {
        return -1;  /* unknown section/name, error */
    }
    return 1;
}

void print_nmda_init_config(nmda_init_config_t* config_struct) {
    ESP_LOGI(TAG, "---- CONFIG FROM INI FILE ----\n");
    ESP_LOGI(TAG, "wifi_essid: %s\n", config_struct->wifi_essid);
    ESP_LOGI(TAG, "wifi_password: %s\n", config_struct->wifi_password);
    ESP_LOGI(TAG, "wifi_ntp_server: %s\n", config_struct->wifi_ntp_server);
    ESP_LOGI(TAG, "mqtt_server: %s\n", config_struct->mqtt_server);
    ESP_LOGI(TAG, "mqtt_trasnport: %s\n", config_struct->mqtt_transport);
    ESP_LOGI(TAG, "mqtt_port: %s\n", config_struct->mqtt_port);
    ESP_LOGI(TAG, "mqtt_user: %s\n", config_struct->mqtt_user);
    ESP_LOGI(TAG, "mqtt_password: %s\n", config_struct->mqtt_password);
    ESP_LOGI(TAG, "mqtt_ca_cert: %s\n", config_struct->mqtt_ca_cert);
    ESP_LOGI(TAG, "mqtt_station: %s\n", config_struct->mqtt_station);
    ESP_LOGI(TAG, "mqtt_experiment: %s\n", config_struct->mqtt_experiment);
    ESP_LOGI(TAG, "mqtt_device_id: %s\n", config_struct->mqtt_device_id);
}

int init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGE("LOAD_SETTING_FROM_NVS", "Error (%s) initializing NVS!\n", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    return ESP_OK;
}

int load_settings_from_sdcard(nmda_init_config_t * nmda_config) {
    ESP_LOGI(TAG, "Trying to load settings from SD card");
  
    // Initialize SD card so we can try to read the configuration from
    // the SD card.
    if (init_sd_card() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card");
    } else {
        ESP_LOGI(TAG, "SD card initialized");
        // Try to find a file named "nmda.ini" and parse it.
        if (ini_parse("/sdcard/nmda.ini", nmda_init_handler, nmda_config) < 0) {
            ESP_LOGW("APP_MAIN", "Can't load 'nmda.ini'\n");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

int load_settings_from_nvs() {
    esp_err_t err;
    
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
    /**
    settings_get_str("wifi_ssid", ssid, &buffer_size);
    settings_get_str("wifi_pass", password, &buffer_size2);
	ESP_LOGI("WIFI", "SSID: %s(%d) -- PASS: %s(%d)", ssid, buffer_size, password, buffer_size2);
    */

    return 0;
}

int settings_get_str(const char * key, char * value, size_t* max_length) {
    esp_err_t err;
    
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

esp_err_t load_nmda_settings(nmda_init_config_t* nmda_config) {
    esp_err_t err;

    err = load_settings_from_sdcard(nmda_config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Settings loaded from SD card");
        return ESP_OK;
    }
    // If we can't load the settings from the SD card, try to load them from the NVS

    err = load_settings_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGE("LOAD_SETTING_FROM_NVS", "Error loading settings from NVS");
        return err;
    }
    return ESP_FAIL;
}