#include "mqtt.h"

#define TAG "MSS_SEND"


void mss_sender(void *parameters) {
	struct telemetry_message message;
    int64_t last_event_time = 0;
    nmda_init_config_t* nmda_config = (nmda_init_config_t*) parameters;
    char topic_base[80];
    char topic_status[80 + strlen("status") + 1];
    char topic_pcnt[80 + strlen("pcnt") + 1];
    char topic_detect[80 + strlen("detect") + 1];
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
    sprintf(topic_detect, "%s/detect", topic_base);
    sprintf(topic_timesync, "%s/timesync", topic_base);
#ifdef CONFIG_ENABLE_SPL06
    sprintf(topic_spl06, "%s/spl06", topic_base);
#endif

	ESP_LOGI(TAG, "Topic base: %s", topic_base);

	mqtt_setup(nmda_config);
	mqtt_send_mss(topic_status, "mss_sender is running");

	while(true) {
		if (xQueueReceive(telemetry_queue, &message, portMAX_DELAY)) {
		    char buffer[500];
		    switch (message.tm_message_type) {
			case TM_METEO:
			    break;

			case TM_PULSE_COUNT:
                sprintf(buffer, "{ \"datetime\": \"%lld\", \"ch01\": \"%lu\", \"ch02\": \"%lu\", \"ch03\": \"%lu\", \"Interval_s\": \"%u\" }",
                    message.timestamp,
                    message.payload.tm_pcnt.channel[0],
                    message.payload.tm_pcnt.channel[1],
                    message.payload.tm_pcnt.channel[2],
                    message.payload.tm_pcnt.integration_time_sec
                );

		        mqtt_send_mss(topic_pcnt, buffer);
		        ESP_LOGI(TAG, "Publishing PULSECOUNT on %s", topic_pcnt);
			    break;

            case TM_PULSE_DETECTION:
                sprintf(buffer, "{ \"datetime\": \"%lld\", \"ch01\": \"%lu\", \"ch02\": \"%lu\", \"ch03\": \"%lu\"}",
                    message.timestamp,
                    message.payload.tm_detect.channel[0],
                    message.payload.tm_detect.channel[1],
                    message.payload.tm_detect.channel[2]
                );
		        ESP_LOGI(TAG, "Publishing DETECTOR %lu,%lu,%lu at %llu delta %llu",
                    message.payload.tm_detect.channel[0],
                    message.payload.tm_detect.channel[1],
                    message.payload.tm_detect.channel[2],
                    message.timestamp,
                    message.timestamp - last_event_time
                );
                last_event_time = message.timestamp;
		        mqtt_send_mss(topic_detect, buffer);
			    break;

            case TM_TIME_SYNCHRONIZER:
                sprintf(buffer, "{ \"datetime\": \"%lld\", \"cpu_lnd\": \"%lu\" }",
                    message.timestamp,
                    message.payload.tm_sync.cpu_count
                );
                ESP_LOGI(TAG, "Publishing TIME_SYNC on %s", topic_timesync);
		        mqtt_send_mss(topic_timesync, buffer);
                break;

#ifdef CONFIG_ENABLE_SPL06
            case TM_SPL06:
                sprintf(buffer, "{ \"datetime\": \"%lld\", \"pressure_pa\": \"%.2f\", \"pressure_hpa\": \"%.2f\", \"temperature_celsius\": \"%.2f\", \"qnh_hpa\": \"%.2f\" }",
                    message.timestamp,
                    message.payload.tm_spl06.pressure_pa,
                    message.payload.tm_spl06.pressure_hpa,
                    message.payload.tm_spl06.temperature_celsius,
                    message.payload.tm_spl06.qnh_hpa
                );
                ESP_LOGI(TAG, "Publishing SPL06 on %s", topic_spl06);
                mqtt_send_mss(topic_spl06, buffer);
                break;
#endif

			default:
			    break;
		    }
		}
	}
}
