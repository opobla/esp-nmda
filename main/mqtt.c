#include "mqtt.h"
#include "settings.h"

esp_mqtt_client_handle_t client = NULL;

struct mqtt_settings_t mqtt_settings;


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("MQTT", "MQTT_EVENT_CONNECTED");
            xSemaphoreGive(mqtt_semaphore);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI("MQTT", "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI("MQTT", "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI("MQTT", "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            //ESP_LOGI("MQTT", "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI("MQTT", "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI("MQTT", "MQTT_EVENT_ERROR");
            break;

        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGE("MQTT", "MQTT_EVENT_BEFORE_CONNECT");
            break;

        default:
            ESP_LOGI("MQTT", "Other event id:%d", event->event_id);
            break;
    }
}

void mqtt_setup(nmda_init_config_t* nmda_config) {
    ESP_LOGI("MQTT_SETUP", "INIT CLIENT");
    char* mqtt_server = nmda_config->mqtt_server;
    char* mqtt_port   = nmda_config->mqtt_port;
    char* mqtt_user   = nmda_config->mqtt_user;
    char* mqtt_pass   = nmda_config->mqtt_password;
    char* mqtt_transport = nmda_config->mqtt_transport;
    char* mqtt_ca_cert   = nmda_config->mqtt_ca_cert;

    // Check for NULL pointers before using them
    if (!mqtt_transport) mqtt_transport = "mqtt";
    if (!mqtt_server) mqtt_server = "unknown";
    if (!mqtt_port) mqtt_port = "unknown";

    ESP_LOGI("MQTT_SETUP", "MQTT trying transport %s host %s and port %s", mqtt_transport, mqtt_server, mqtt_port);
    ESP_LOGI("MQTT_SETUP", "MQTT trying certificate %s", mqtt_ca_cert ? mqtt_ca_cert : "(null)");

    esp_mqtt_client_config_t mqttConfig = {};
    if (strcmp(mqtt_transport, "mqtt") == 0) {
        mqttConfig.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    } else {
        mqttConfig.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
    }

    mqttConfig.broker.address.hostname = mqtt_server;
    mqttConfig.broker.address.port = atoi(mqtt_port);
    mqttConfig.credentials.username = mqtt_user;
    mqttConfig.credentials.authentication.password = mqtt_pass;
    mqttConfig.broker.verification.certificate = mqtt_ca_cert;

    ESP_LOGI("MSS_SEND", "MQTT initializing");
    client = esp_mqtt_client_init(&mqttConfig);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
    return (void)client;
}

void mqtt_send_mss(char* topic, char* mss) {
    esp_mqtt_client_publish(client, topic, mss, 0, 0, false);
}
