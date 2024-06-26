#ifndef __ESP32_LIBS_H_
#define __ESP32_LIBS_H_

#include <stdio.h>
#include <string.h>

#include "nvs_flash.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "sdkconfig.h"

#ifndef CONFIG_LOG_MAXIMUM_LEVEL
#define CONFIG_LOG_MAXIMUM_LEVEL 4
#endif

#endif
