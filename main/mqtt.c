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

void mqtt_setup() {
    ESP_LOGI("MQTT_SETUP", "INIT CLIENT");
    const int buffer_size = 64;
    char mqtt_host[buffer_size];
    char mqtt_port[buffer_size];
    char mqtt_user[buffer_size];
    char mqtt_pass[buffer_size];

    settings_get_str("mqtt_host", mqtt_host, &buffer_size);
    settings_get_str("mqtt_port", mqtt_port, &buffer_size);
    if (settings_get_str("mqtt_user", mqtt_user, &buffer_size) == ESP_OK) {
        ESP_LOGI("MQTT_SETUP", "MQTT using user %s", mqtt_user);
    } else {
        ESP_LOGI("MQTT_SETUP", "MQTT not using user");
        mqtt_user[0] = '\0';
    }
    if (settings_get_str("mqtt_pass", mqtt_pass, &buffer_size) == ESP_OK) {
        ESP_LOGI("MQTT_SETUP", "MQTT using password %s", "*** hidden ***");
    } else {
        ESP_LOGI("MQTT_SETUP", "MQTT not using password");
        mqtt_pass[0] = '\0';
    }
    
    ESP_LOGI("MQTT_SETUP", "MQTT trying host %s and port %s", mqtt_host, mqtt_port);

    esp_mqtt_client_config_t mqttConfig = {
      .broker.address.hostname = mqtt_host,
      //.credentials.username = mqtt_user,
      //.credentials.authentication.password = mqtt_pass,
      .broker.address.port = atoi(mqtt_port),
      .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
    };

    ESP_LOGI("MSS_SEND", "MQTT initializing");
    client = esp_mqtt_client_init(&mqttConfig);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
    return (void)client;
}

void mqtt_send_mss(char* topic, char* mss) {
    esp_mqtt_client_publish(client, topic, mss, 0, 0, false);
}
