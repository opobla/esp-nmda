#ifndef __MQTT__
#define __MQTT__

#include "common.h"
#include "datastructures.h"

#include "mqtt_client.h"
#include "settings.h"

void mqtt_setup(nmda_init_config_t* nmda_config);
void mqtt_send_mss(char* topic, char* mss);
void mss_sender(void *parameters);

struct mqtt_settings_t {
    char* host;
    char* port;
    char* user;
    char* pass;
    char* topic;
};

#endif
