#ifndef __MQTT__
#define __MQTT__

#include "common.h"
#include "datastructures.h"

#include "mqtt_client.h"

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
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
