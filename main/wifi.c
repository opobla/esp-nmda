#include "wifi.h"
#include "settings.h"

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	switch (event_id) {
		case WIFI_EVENT_STA_START: {
			ESP_LOGI("WIFI","start");
        	esp_wifi_connect();
			break;
		}
		case WIFI_EVENT_STA_CONNECTED: {
			ESP_LOGI("WIFI","connected");
			break;
		}
		case WIFI_EVENT_STA_DISCONNECTED: {
			ESP_LOGI("WIFI","disconnected trying to reconect in %d seconds", time_to_reconect);
			wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
			ESP_LOGI("WIFI","disconnected reason: %d", event->reason);
			vTaskDelay(time_to_reconect * 5000 / portTICK_PERIOD_MS);
            esp_restart();
			break;
		}
		case IP_EVENT_STA_GOT_IP: {
			ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
			ESP_LOGI("WIFI", "IP(" IPSTR ")", IP2STR(&event->ip_info.ip));
			xSemaphoreGive(wifi_semaphore);
			ESP_LOGI("WIFI", "wifi_semaphore unlocked");
			break;
		}
		default: {
			printf("event_id: %d\n", (int) event_id);
			break;
		}
	}
}

void wifi_setup(nmda_init_config_t* nmda_config) {

	ESP_LOGI("WIFI", "wifi_setup: starting");

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event_handler,NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event_handler,NULL));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = {0},
			.password = {0},
			.scan_method = (wifi_scan_method_t)0,
			.bssid_set = false,
			.bssid = {0},
			.channel = 0,
			.listen_interval = 0,
			.sort_method = (wifi_sort_method_t)0,
			.threshold = {0, (wifi_auth_mode_t)0}},
	};
	memset(&wifi_config, 0, sizeof(wifi_config));

	strcpy((char*)wifi_config.sta.ssid,     nmda_config->wifi_essid);
	strcpy((char*)wifi_config.sta.password, nmda_config->wifi_password);
	ESP_LOGI("WIFI", "wifi_essid: |%s|", wifi_config.sta.ssid);
	ESP_LOGI("WIFI", "wifi_password: |%s|", wifi_config.sta.password);

	wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	wifi_config.sta.pmf_cfg.capable = true;
	wifi_config.sta.pmf_cfg.required = false;

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI("WIFI", "wifi_setup: done");
	printf("------------------------------\n");
	xSemaphoreTake(wifi_semaphore, portMAX_DELAY);
}
