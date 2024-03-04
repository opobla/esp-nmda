#ifndef SETTINGS_H
#define SETTINGS_H

int load_settings_from_nvs();

#include <stddef.h>


int settings_get_str(const char * key, char * value, size_t* max_length);

#endif