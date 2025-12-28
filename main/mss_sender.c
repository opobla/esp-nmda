#include "mqtt.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <inttypes.h>
#include "cJSON.h"

#define TAG "MSS_SEND"


void mss_sender(void *parameters) {
	struct telemetry_message message;
#ifdef CONFIG_ENABLE_GPIO_PULSE_DETECTION
    int64_t last_event_time = 0;
#endif
    nmda_init_config_t* nmda_config = (nmda_init_config_t*) parameters;
    char topic_base[80];
    char topic_status[80 + strlen("status") + 1];
    char topic_pcnt[80 + strlen("pcnt") + 1];
#ifdef CONFIG_ENABLE_GPIO_PULSE_DETECTION
    char topic_detect[80 + strlen("detect") + 1];
#endif
#ifdef CONFIG_ENABLE_RMT_PULSE_DETECTION
    char topic_rmt_pulse[80 + strlen("rmt_pulse") + 1];
#endif
    char topic_timesync[80 + strlen("timesync") + 1];
#ifdef CONFIG_ENABLE_SPL06
    char topic_spl06[80 + strlen("spl06") + 1];
#endif
    char* station = nmda_config->mqtt_station;
    char* experiment = nmda_config->mqtt_experiment;
    char* device = nmda_config->mqtt_device_id;

    // Check for NULL pointers and use defaults if needed
    if (!station) station = "default";
    if (!experiment) experiment = "default";
    if (!device) device = "default";

	ESP_LOGI("MSS_SEND", "is running on %d Core", xPortGetCoreID());

    sprintf(topic_base, "%s/%s/%s", station, experiment, device);
    sprintf(topic_status, "%s/status", topic_base);
    sprintf(topic_pcnt, "%s/pcnt", topic_base);
#ifdef CONFIG_ENABLE_GPIO_PULSE_DETECTION
    sprintf(topic_detect, "%s/detect", topic_base);
#endif
#ifdef CONFIG_ENABLE_RMT_PULSE_DETECTION
    sprintf(topic_rmt_pulse, "%s/rmt_pulse", topic_base);
#endif
    sprintf(topic_timesync, "%s/timesync", topic_base);
#ifdef CONFIG_ENABLE_SPL06
    sprintf(topic_spl06, "%s/spl06", topic_base);
#endif

	ESP_LOGI(TAG, "Topic base: %s", topic_base);

	mqtt_setup(nmda_config);
	
	// Wait for MQTT connection
	ESP_LOGI(TAG, "Waiting for MQTT connection...");
	if (xSemaphoreTake(mqtt_semaphore, pdMS_TO_TICKS(30000)) != pdTRUE) {
		ESP_LOGE(TAG, "MQTT connection timeout");
	} else {
		ESP_LOGI(TAG, "MQTT connected successfully");
	}
	
	mqtt_send_mss(topic_status, "mss_sender is running");
	ESP_LOGI(TAG, "MQTT sender ready");

	while(true) {
		ESP_LOGD(TAG, "Waiting for message from telemetry queue...");
		if (xQueueReceive(telemetry_queue, &message, portMAX_DELAY)) {
		    char *json_string = NULL;
		    cJSON *json = NULL;
		    
		    switch (message.tm_message_type) {
			case TM_METEO:
			    break;

			case TM_PULSE_COUNT:
                {
                    json = cJSON_CreateObject();
                    if (json == NULL) {
                        ESP_LOGE(TAG, "Failed to create JSON object for PULSE_COUNT");
                        break;
                    }
                    
                    // Create string values for timestamps and counts (as per original format)
                    char start_ts_str[32];
                    char end_ts_str[32];
                    char ch01_str[32];
                    char ch02_str[32];
                    char ch03_str[32];
                    char interval_str[16];
                    
                    snprintf(start_ts_str, sizeof(start_ts_str), "%lld", message.payload.tm_pcnt.start_timestamp);
                    snprintf(end_ts_str, sizeof(end_ts_str), "%lld", message.timestamp);
                    snprintf(ch01_str, sizeof(ch01_str), "%lu", (unsigned long)message.payload.tm_pcnt.channel[0]);
                    snprintf(ch02_str, sizeof(ch02_str), "%lu", (unsigned long)message.payload.tm_pcnt.channel[1]);
                    snprintf(ch03_str, sizeof(ch03_str), "%lu", (unsigned long)message.payload.tm_pcnt.channel[2]);
                    snprintf(interval_str, sizeof(interval_str), "%u", message.payload.tm_pcnt.integration_time_sec);
                    
                    cJSON_AddStringToObject(json, "start_datetime", start_ts_str);
                    cJSON_AddStringToObject(json, "datetime", end_ts_str);
                    cJSON_AddStringToObject(json, "ch01", ch01_str);
                    cJSON_AddStringToObject(json, "ch02", ch02_str);
                    cJSON_AddStringToObject(json, "ch03", ch03_str);
                    cJSON_AddStringToObject(json, "Interval_s", interval_str);
                    
                    json_string = cJSON_PrintUnformatted(json);
                    if (json_string == NULL) {
                        ESP_LOGE(TAG, "Failed to print JSON for PULSE_COUNT");
                        cJSON_Delete(json);
                        break;
                    }
                    
                    ESP_LOGI(TAG, "Publishing PULSECOUNT on %s: ch1=%lu, ch2=%lu, ch3=%lu, interval=%u", 
                             topic_pcnt, 
                             (unsigned long)message.payload.tm_pcnt.channel[0],
                             (unsigned long)message.payload.tm_pcnt.channel[1],
                             (unsigned long)message.payload.tm_pcnt.channel[2],
                             message.payload.tm_pcnt.integration_time_sec);
                    mqtt_send_mss(topic_pcnt, json_string);
                    ESP_LOGI(TAG, "PULSECOUNT message published successfully");
                    
                    free(json_string);
                    cJSON_Delete(json);
                }
			    break;

#ifdef CONFIG_ENABLE_GPIO_PULSE_DETECTION
            case TM_PULSE_DETECTION:
                {
                    json = cJSON_CreateObject();
                    if (json == NULL) {
                        ESP_LOGE(TAG, "Failed to create JSON object for PULSE_DETECTION");
                        break;
                    }
                    
                    char ts_str[32];
                    char ch01_str[32];
                    char ch02_str[32];
                    char ch03_str[32];
                    
                    snprintf(ts_str, sizeof(ts_str), "%lld", message.timestamp);
                    snprintf(ch01_str, sizeof(ch01_str), "%lu", (unsigned long)message.payload.tm_detect.channel[0]);
                    snprintf(ch02_str, sizeof(ch02_str), "%lu", (unsigned long)message.payload.tm_detect.channel[1]);
                    snprintf(ch03_str, sizeof(ch03_str), "%lu", (unsigned long)message.payload.tm_detect.channel[2]);
                    
                    cJSON_AddStringToObject(json, "datetime", ts_str);
                    cJSON_AddStringToObject(json, "ch01", ch01_str);
                    cJSON_AddStringToObject(json, "ch02", ch02_str);
                    cJSON_AddStringToObject(json, "ch03", ch03_str);
                    
                    json_string = cJSON_PrintUnformatted(json);
                    if (json_string == NULL) {
                        ESP_LOGE(TAG, "Failed to print JSON for PULSE_DETECTION");
                        cJSON_Delete(json);
                        break;
                    }
                    
                    ESP_LOGI(TAG, "Publishing DETECTOR %lu,%lu,%lu at %lld delta %lld us",
                             (unsigned long)message.payload.tm_detect.channel[0],
                             (unsigned long)message.payload.tm_detect.channel[1],
                             (unsigned long)message.payload.tm_detect.channel[2],
                             message.timestamp,
                             message.timestamp - last_event_time);
                    last_event_time = message.timestamp;
                    mqtt_send_mss(topic_detect, json_string);
                    
                    free(json_string);
                    cJSON_Delete(json);
                }
			    break;
#endif

#ifdef CONFIG_ENABLE_RMT_PULSE_DETECTION
            case TM_RMT_PULSE_EVENT:
                {
                    // Validate message data first
                    if (message.payload.tm_rmt_pulse_event.pulses == NULL) {
                        ESP_LOGE(TAG, "RMT pulse event has NULL pulses array (symbols=%u)", 
                                message.payload.tm_rmt_pulse_event.symbols);
                        break;
                    }
                    
                    if (message.payload.tm_rmt_pulse_event.symbols == 0) {
                        ESP_LOGW(TAG, "RMT pulse event has 0 symbols, skipping");
                        // Free pulses array if it exists
                        if (message.payload.tm_rmt_pulse_event.pulses != NULL) {
                            heap_caps_free(message.payload.tm_rmt_pulse_event.pulses);
                        }
                        break;
                    }
                    
                    // Validate topic is not empty (topic_rmt_pulse is an array, not a pointer)
                    if (topic_rmt_pulse[0] == '\0') {
                        ESP_LOGE(TAG, "RMT topic is empty, not sending");
                        // Free pulses array
                        heap_caps_free(message.payload.tm_rmt_pulse_event.pulses);
                        break;
                    }
                    
                    // Create JSON object
                    json = cJSON_CreateObject();
                    if (json == NULL) {
                        ESP_LOGE(TAG, "Failed to create JSON object for RMT_PULSE_EVENT");
                        heap_caps_free(message.payload.tm_rmt_pulse_event.pulses);
                        break;
                    }
                    
                    // Add start_datetime (as string to match original format)
                    char start_ts_str[32];
                    snprintf(start_ts_str, sizeof(start_ts_str), "%" PRId64, 
                            message.payload.tm_rmt_pulse_event.start_timestamp);
                    cJSON_AddStringToObject(json, "start_datetime", start_ts_str);
                    
                    // Add channel (as string "ch1", "ch2", "ch3")
                    char channel_str[8];
                    snprintf(channel_str, sizeof(channel_str), "ch%u", 
                            message.payload.tm_rmt_pulse_event.channel);
                    cJSON_AddStringToObject(json, "channel", channel_str);
                    
                    // Add symbols count
                    cJSON_AddNumberToObject(json, "symbols", message.payload.tm_rmt_pulse_event.symbols);
                    
                    // Create pulses array
                    cJSON *pulses_array = cJSON_CreateArray();
                    if (pulses_array == NULL) {
                        ESP_LOGE(TAG, "Failed to create pulses array");
                        cJSON_Delete(json);
                        heap_caps_free(message.payload.tm_rmt_pulse_event.pulses);
                        break;
                    }
                    
                    // Add each pulse as an object to the array
                    for (uint8_t i = 0; i < message.payload.tm_rmt_pulse_event.symbols; i++) {
                        cJSON *pulse_obj = cJSON_CreateObject();
                        if (pulse_obj == NULL) {
                            ESP_LOGW(TAG, "Failed to create pulse object %u, stopping at %u pulses", 
                                    i, i);
                            break;
                        }
                        
                        // Add duration_us and separation_us as numbers
                        cJSON_AddNumberToObject(pulse_obj, "duration_us", 
                                message.payload.tm_rmt_pulse_event.pulses[i].duration_us);
                        cJSON_AddNumberToObject(pulse_obj, "separation_us", 
                                message.payload.tm_rmt_pulse_event.pulses[i].separation_us);
                        
                        // Add pulse object to array
                        cJSON_AddItemToArray(pulses_array, pulse_obj);
                    }
                    
                    // Add pulses array to main object
                    cJSON_AddItemToObject(json, "pulses", pulses_array);
                    
                    // Print JSON to string
                    json_string = cJSON_PrintUnformatted(json);
                    if (json_string == NULL) {
                        ESP_LOGE(TAG, "Failed to print JSON for RMT_PULSE_EVENT");
                        cJSON_Delete(json);
                        heap_caps_free(message.payload.tm_rmt_pulse_event.pulses);
                        break;
                    }
                    
                    // Send via MQTT
                    mqtt_send_mss(topic_rmt_pulse, json_string);
                    
                    // Free JSON string and object
                    free(json_string);
                    cJSON_Delete(json);
                    
                    // Free the dynamically allocated pulses array after sending
                    heap_caps_free(message.payload.tm_rmt_pulse_event.pulses);
                    message.payload.tm_rmt_pulse_event.pulses = NULL;
                }
                break;
#endif

            case TM_TIME_SYNCHRONIZER:
                {
                    json = cJSON_CreateObject();
                    if (json == NULL) {
                        ESP_LOGE(TAG, "Failed to create JSON object for TIME_SYNCHRONIZER");
                        break;
                    }
                    
                    char ts_str[32];
                    char cpu_str[32];
                    
                    snprintf(ts_str, sizeof(ts_str), "%lld", message.timestamp);
                    snprintf(cpu_str, sizeof(cpu_str), "%lu", (unsigned long)message.payload.tm_sync.cpu_count);
                    
                    cJSON_AddStringToObject(json, "datetime", ts_str);
                    cJSON_AddStringToObject(json, "cpu_lnd", cpu_str);
                    
                    json_string = cJSON_PrintUnformatted(json);
                    if (json_string == NULL) {
                        ESP_LOGE(TAG, "Failed to print JSON for TIME_SYNCHRONIZER");
                        cJSON_Delete(json);
                        break;
                    }
                    
                    ESP_LOGI(TAG, "Publishing TIME_SYNC on %s", topic_timesync);
                    mqtt_send_mss(topic_timesync, json_string);
                    
                    free(json_string);
                    cJSON_Delete(json);
                }
                break;

#ifdef CONFIG_ENABLE_SPL06
            case TM_SPL06:
                {
                    json = cJSON_CreateObject();
                    if (json == NULL) {
                        ESP_LOGE(TAG, "Failed to create JSON object for SPL06");
                        break;
                    }
                    
                    char ts_str[32];
                    char pressure_pa_str[32];
                    char pressure_hpa_str[32];
                    char temp_str[32];
                    char qnh_str[32];
                    
                    snprintf(ts_str, sizeof(ts_str), "%lld", message.timestamp);
                    snprintf(pressure_pa_str, sizeof(pressure_pa_str), "%.2f", message.payload.tm_spl06.pressure_pa);
                    snprintf(pressure_hpa_str, sizeof(pressure_hpa_str), "%.2f", message.payload.tm_spl06.pressure_hpa);
                    snprintf(temp_str, sizeof(temp_str), "%.2f", message.payload.tm_spl06.temperature_celsius);
                    snprintf(qnh_str, sizeof(qnh_str), "%.2f", message.payload.tm_spl06.qnh_hpa);
                    
                    cJSON_AddStringToObject(json, "datetime", ts_str);
                    cJSON_AddStringToObject(json, "pressure_pa", pressure_pa_str);
                    cJSON_AddStringToObject(json, "pressure_hpa", pressure_hpa_str);
                    cJSON_AddStringToObject(json, "temperature_celsius", temp_str);
                    cJSON_AddStringToObject(json, "qnh_hpa", qnh_str);
                    
                    json_string = cJSON_PrintUnformatted(json);
                    if (json_string == NULL) {
                        ESP_LOGE(TAG, "Failed to print JSON for SPL06");
                        cJSON_Delete(json);
                        break;
                    }
                    
                    ESP_LOGI(TAG, "Publishing SPL06 on %s", topic_spl06);
                    mqtt_send_mss(topic_spl06, json_string);
                    ESP_LOGI(TAG, "SPL06 message published successfully");
                    
                    free(json_string);
                    cJSON_Delete(json);
                }
                break;
#endif

			default:
			    ESP_LOGW(TAG, "Unknown message type: %d", message.tm_message_type);
			    break;
		    }
		    ESP_LOGD(TAG, "Message processing completed, waiting for next message...");
		} else {
		    ESP_LOGW(TAG, "Failed to receive message from queue");
		}
	}
}
