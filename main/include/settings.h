#ifndef SETTINGS_H
#define SETTINGS_H

#include <stddef.h>

typedef struct {
    char* wifi_essid;
    char* wifi_password;
    char* wifi_ntp_server;
    char* mqtt_server;
    char* mqtt_port;
    char* mqtt_user;
    char* mqtt_password;
    char* mqtt_transport;
    char* mqtt_ca_cert;
    char* mqtt_station;
    char* mqtt_experiment;
    char* mqtt_device_id;
} nmda_init_config_t;

#define NMDA_INIT_CONFIG_DEFAULT() {\
    .wifi_essid = "default",\
    .wifi_password = "default",\
    .wifi_ntp_server = "default",\
    .mqtt_server = "default",\
    .mqtt_port = "default",\
    .mqtt_user = "default",\
    .mqtt_password = "default",\
    .mqtt_transport = "mqtt",\
    .mqtt_ca_cert = (char*)NULL,\
    .mqtt_station = "default",\
    .mqtt_experiment = "default",\
    .mqtt_device_id = "default"\
}; 


// Functions to read configuration from an ini file from the SD card
int nmda_init_handler(void* config_struct, const char* section, const char* name, const char* value);
void print_nmda_init_config(nmda_init_config_t* config_struct);

// Functions to read configuration from the NVS
int settings_get_str(const char * key, char * value, size_t* max_length);

// Functions to load the configuration from the SD card or the NVS
esp_err_t load_nmda_settings(nmda_init_config_t* nmda_config);

// Functions to initialize the NVS
esp_err_t init_nvs();


#endif