#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"
#include "esp_partition.h"
#include "settings.h"
#include "ini.h"
#include <string.h>
#include <stdlib.h>
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
    ESP_LOGI(TAG, "---- CONFIG ----\n");
    ESP_LOGI(TAG, "wifi_essid: %s\n", config_struct->wifi_essid ? config_struct->wifi_essid : "(null)");
    ESP_LOGI(TAG, "wifi_password: %s\n", config_struct->wifi_password ? config_struct->wifi_password : "(null)");
    ESP_LOGI(TAG, "wifi_ntp_server: %s\n", config_struct->wifi_ntp_server ? config_struct->wifi_ntp_server : "(null)");
    ESP_LOGI(TAG, "mqtt_server: %s\n", config_struct->mqtt_server ? config_struct->mqtt_server : "(null)");
    ESP_LOGI(TAG, "mqtt_transport: %s\n", config_struct->mqtt_transport ? config_struct->mqtt_transport : "(null)");
    ESP_LOGI(TAG, "mqtt_port: %s\n", config_struct->mqtt_port ? config_struct->mqtt_port : "(null)");
    ESP_LOGI(TAG, "mqtt_user: %s\n", config_struct->mqtt_user ? config_struct->mqtt_user : "(null)");
    ESP_LOGI(TAG, "mqtt_password: %s\n", config_struct->mqtt_password ? config_struct->mqtt_password : "(null)");
    ESP_LOGI(TAG, "mqtt_ca_cert: %s\n", config_struct->mqtt_ca_cert ? config_struct->mqtt_ca_cert : "(null)");
    ESP_LOGI(TAG, "mqtt_station: %s\n", config_struct->mqtt_station ? config_struct->mqtt_station : "(null)");
    ESP_LOGI(TAG, "mqtt_experiment: %s\n", config_struct->mqtt_experiment ? config_struct->mqtt_experiment : "(null)");
    ESP_LOGI(TAG, "mqtt_device_id: %s\n", config_struct->mqtt_device_id ? config_struct->mqtt_device_id : "(null)");
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
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "SD card initialized");
    // Try to find a file named "nmda.ini" and parse it.
    if (ini_parse("/sdcard/nmda.ini", nmda_init_handler, nmda_config) < 0) {
        ESP_LOGW(TAG, "Can't load 'nmda.ini'");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t load_settings_from_nvs(nmda_init_config_t* nmda_config) {
    esp_err_t err;
    char buffer[256];
    size_t buffer_size;
    
    ESP_LOGI(TAG, "Loading settings from NVS");
    err = nvs_flash_init_partition(NVS_PARTITION_NAME);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS partition '%s' not found or not initialized", NVS_PARTITION_NAME);
            ESP_LOGW(TAG, "This is normal on first boot. Settings will use defaults.");
            return ESP_ERR_NVS_NOT_FOUND;
        } else if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS partition needs erasing: %s", esp_err_to_name(err));
            // Try to erase and reinitialize
            const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 
                                                                        ESP_PARTITION_SUBTYPE_DATA_NVS, 
                                                                        NVS_PARTITION_NAME);
            if (partition != NULL) {
                ESP_LOGW(TAG, "Erasing NVS partition...");
                err = esp_partition_erase_range(partition, 0, partition->size);
                if (err == ESP_OK) {
                    err = nvs_flash_init_partition(NVS_PARTITION_NAME);
                }
            }
        }
        
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error initializing NVS partition: %s", esp_err_to_name(err));
            return err;
        }
    }

    err = nvs_open_from_partition(NVS_PARTITION_NAME, "settings", NVS_READONLY, &my_nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS namespace 'settings' not found in partition '%s'", NVS_PARTITION_NAME);
            ESP_LOGW(TAG, "This is normal on first boot. Settings will use defaults.");
            return ESP_ERR_NVS_NOT_FOUND;
        }
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }

    // Helper macro to safely free and duplicate string
    #define LOAD_AND_SET(key, field) do { \
        buffer_size = sizeof(buffer); \
        if (settings_get_str(key, buffer, &buffer_size) == 0) { \
            if (nmda_config->field && strcmp(nmda_config->field, "default") != 0) { \
                free(nmda_config->field); \
            } \
            nmda_config->field = strdup(buffer); \
            ESP_LOGI(TAG, "Loaded %s: %s", key, nmda_config->field); \
        } else { \
            ESP_LOGW(TAG, "Failed to load %s from NVS, keeping default", key); \
        } \
    } while(0)

    // Load WiFi settings
    LOAD_AND_SET("wifi_ssid", wifi_essid);
    LOAD_AND_SET("wifi_pasword", wifi_password);
    LOAD_AND_SET("wifi_ntp_server", wifi_ntp_server);

    // Load MQTT settings
    LOAD_AND_SET("mqtt_host", mqtt_server);
    LOAD_AND_SET("mqtt_port", mqtt_port);
    LOAD_AND_SET("mqtt_user", mqtt_user);
    LOAD_AND_SET("mqtt_password", mqtt_password);
    LOAD_AND_SET("mqtt_station", mqtt_station);
    LOAD_AND_SET("mqtt_experiment", mqtt_experiment);
    LOAD_AND_SET("mqtt_device_id", mqtt_device_id);
    
    #undef LOAD_AND_SET

    nvs_close(my_nvs_handle);
    ESP_LOGI(TAG, "Settings loaded from NVS");
    return ESP_OK;
}

int settings_get_str(const char * key, char * value, size_t* max_length) {
    esp_err_t err;
    size_t original_size = *max_length;
    
    err = nvs_get_str(my_nvs_handle, key, value, max_length);
    if (err == ESP_OK) {
        // Successfully read the string from NVS
        // max_length now contains the actual length including null terminator
        ESP_LOGI(TAG, "Key '%s' loaded: '%s' (length: %zu)", key, value, *max_length);
        return 0;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Key '%s' not found in NVS", key);
        *max_length = original_size; // Restore original size
        return err;
    } else {
        ESP_LOGE(TAG, "Error reading key '%s': %s", key, esp_err_to_name(err));
        *max_length = original_size; // Restore original size
        return err;
    }
}

esp_err_t load_nmda_settings(nmda_init_config_t* nmda_config) {
    esp_err_t err;

    // Try to load settings from SD card first
    err = load_settings_from_sdcard(nmda_config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Settings loaded from SD card");
        return ESP_OK;
    }
    
    // If we can't load the settings from the SD card, try to load them from the NVS
    ESP_LOGI(TAG, "SD card load failed, trying NVS");
    err = load_settings_from_nvs(nmda_config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Settings loaded from NVS");
        return ESP_OK;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        // NVS partition or namespace doesn't exist yet - this is OK on first boot
        ESP_LOGW(TAG, "NVS settings not found (first boot or partition not initialized)");
        ESP_LOGW(TAG, "Using default settings. You can configure via SD card or initialize NVS partition.");
        return ESP_OK;  // Return OK to allow boot with defaults
    }
    
    ESP_LOGW(TAG, "Failed to load settings from both SD card and NVS, using defaults");
    return ESP_OK;  // Return OK instead of FAIL to allow boot with defaults
}