#ifndef SDCARD_H
#define SDCARD_H
#include "esp_err.h"

esp_err_t init_sd_card();
void write_file(char *path, char *content);

#endif // SDCARD_H
