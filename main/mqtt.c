#include "mqtt.h"
#include "settings.h"
#include <string.h>

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
            ESP_LOGI("MQTT", "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI("MQTT", "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE("MQTT", "MQTT_EVENT_ERROR: error_handle=%p", event->error_handle);
            if (event->error_handle != NULL) {
                ESP_LOGE("MQTT", "Error type: %d, error code: %d", 
                         event->error_handle->error_type,
                         event->error_handle->esp_transport_sock_errno);
            }
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
    ESP_LOGI("MQTT_SETUP", "MQTT CA certificate: %s", mqtt_ca_cert ? "configured" : "not configured (NULL)");

    esp_mqtt_client_config_t mqttConfig = {};
    // Determine transport type: "mqtt" = TCP, "mqtts" or "ssl" = TLS/SSL
    if (strcmp(mqtt_transport, "mqtt") == 0) {
        mqttConfig.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
        ESP_LOGI("MQTT_SETUP", "Using TCP transport (no encryption)");
    } else if (strcmp(mqtt_transport, "mqtts") == 0 || strcmp(mqtt_transport, "ssl") == 0 || strcmp(mqtt_transport, "tls") == 0) {
        mqttConfig.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
        ESP_LOGI("MQTT_SETUP", "Using TLS/SSL transport");
        if (!mqtt_ca_cert) {
            ESP_LOGW("MQTT_SETUP", "TLS enabled but no CA certificate configured - connection may fail!");
        }
    } else {
        // Default to SSL if transport is not recognized (backward compatibility)
        mqttConfig.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
        ESP_LOGW("MQTT_SETUP", "Unknown transport '%s', defaulting to SSL/TLS", mqtt_transport);
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
    if (client == NULL) {
        ESP_LOGW("MQTT", "Cannot publish: MQTT client not initialized");
        return;
    }
    
    // Validate pointers before using (avoid calling strlen on NULL)
    if (topic == NULL) {
        ESP_LOGE("MQTT", "Cannot publish: topic is NULL");
        return;
    }
    
    if (mss == NULL) {
        ESP_LOGE("MQTT", "Cannot publish: message is NULL");
        return;
    }
    
    // Additional safety: check that strings are not empty and are valid
    // esp_mqtt_client_publish internally calls strlen, which will panic on NULL
    if (topic[0] == '\0') {
        ESP_LOGE("MQTT", "Cannot publish: topic is empty string");
        return;
    }
    
    if (mss[0] == '\0') {
        ESP_LOGW("MQTT", "Warning: message is empty string, publishing anyway");
    }
    
    int msg_id = esp_mqtt_client_publish(client, topic, mss, 0, 0, false);
    if (msg_id < 0) {
        ESP_LOGE("MQTT", "Failed to publish message to %s (error: %d)", topic ? topic : "(null)", msg_id);
    }
}
