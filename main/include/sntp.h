#ifndef __SNTP__
#define __SNTP__

#include "common.h"

#include "esp_sntp.h"

void print_time(const time_t time, const char *message);
void on_got_time(struct timeval *tv);

#endif
