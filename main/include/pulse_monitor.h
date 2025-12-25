#ifndef __P_MONITOR_H_
#define __P_MONITOR_H_

#include "common.h"
#include "datastructures.h"
#include "esp_err.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"  // Migrado a nuevo driver

#define PIN_PULSE_IN_CH1 25
#define PIN_PULSE_IN_CH2 26
#define PIN_PULSE_IN_CH3 27

// Nuevas funciones con manejo de errores y uso de índices
esp_err_t pulse_counter_init(int channel_index, int pulse_gpio_num);
esp_err_t pulse_counter_deinit(int channel_index);
int16_t get_and_clear(int channel_index);

#endif
