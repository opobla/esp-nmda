#include "mqtt.h"

# define STATION  "orca"
# define EXPERIMENT "nemo"
# define DEVICE "bp28-nemo"
# define SLASH "/"

# define BASE_TOPIC STATION SLASH EXPERIMENT SLASH DEVICE

void mss_sender(void *parameters) {
	struct telemetry_message message;
    int64_t last_event_time = 0;


	ESP_LOGI("MSS_SEND", "is running on %d Core", xPortGetCoreID());

	mqtt_setup();
	mqtt_send_mss("test", "esp32 connected");

	while(true) {
		if (xQueueReceive(telemetry_queue, &message, portMAX_DELAY)) {
		    char buffer[500];
            char full_topic[80];
		    switch (message.tm_message_type) {
			case TM_METEO:
                sprintf(buffer, "{ \"datetime\": \"%lld\", \"temp_c\": \"%.2f\", \"atmpres_Pa\": \"%lu\" }",
                        message.timestamp,
                        message.payload.tm_meteo.temperature_celsius,
                        message.payload.tm_meteo.atm_pressure_hpas
                );

                sprintf(full_topic, "%s/meteo", BASE_TOPIC);
		        mqtt_send_mss(full_topic, buffer);
		        ESP_LOGI("MSS_SEND", "Publishing METEO");
			    break;

			case TM_PULSE_COUNT:
                sprintf(buffer, "{ \"datetime\": \"%lld\", \"ch01\": \"%lu\", \"ch02\": \"%lu\", \"ch03\": \"%lu\", \"Interval_s\": \"%u\" }",
                    message.timestamp,
                    message.payload.tm_pcnt.channel[0],
                    message.payload.tm_pcnt.channel[1],
                    message.payload.tm_pcnt.channel[2],
                    message.payload.tm_pcnt.integration_time_sec
                );

                sprintf(full_topic, "%s/pcnt", BASE_TOPIC);
		        mqtt_send_mss(full_topic, buffer);
		        ESP_LOGI("MSS_SEND", "Publishing PULSECOUNT on %s", full_topic);
			    break;

            case TM_PULSE_DETECTION:
                sprintf(buffer, "{ \"datetime\": \"%lld\", \"ch01\": \"%lu\", \"ch02\": \"%lu\", \"ch03\": \"%lu\"}",
                    message.timestamp,
                    message.payload.tm_detect.channel[0],
                    message.payload.tm_detect.channel[1],
                    message.payload.tm_detect.channel[2]
                );
		        ESP_LOGI("MSS_SEND", "Publishing DETECTOR %lu,%lu,%lu at %llu delta %llu",
                    message.payload.tm_detect.channel[0],
                    message.payload.tm_detect.channel[1],
                    message.payload.tm_detect.channel[2],
                    message.timestamp,
                    message.timestamp - last_event_time
                );
                last_event_time = message.timestamp;
                sprintf(full_topic, "%s/detect", BASE_TOPIC);
		        mqtt_send_mss(full_topic, buffer);
			    break;


            case TM_TIME_SYNCHRONIZER:
                sprintf(buffer, "{ \"datetime\": \"%lld\", \"cpu_lnd\": \"%lu\" }",
                    message.timestamp,
                    message.payload.tm_sync.cpu_count
                );
                sprintf(full_topic, "%s/sync", BASE_TOPIC);
		        mqtt_send_mss(full_topic, buffer);
                break;

			default:
			    break;
		    }
		}
	}
}
