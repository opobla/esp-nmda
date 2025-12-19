#ifndef __WIFI_H_
#define __WIFI_H_

#include "common.h"
#include "settings.h"

#include "esp_wifi.h"

#define time_to_reconect 5

void wifi_setup(nmda_init_config_t* nmda_config);
#endif
