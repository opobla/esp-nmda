#ifndef __SNTP__
#define __SNTP__

#include "common.h"
#include "settings.h"

#include "esp_sntp.h"

void ntp_setup(nmda_init_config_t* nmda_config);
void print_time(const time_t time, const char *message);
void on_got_time(struct timeval *tv);

#endif
