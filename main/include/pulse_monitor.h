#ifndef __P_MONITOR_H_
#define __P_MONITOR_H_

#include "common.h"
#include "datastructures.h"

#include "driver/gpio.h"
#include "driver/pcnt.h"

#define PIN_PULSE_IN_CH1 25
#define PIN_PULSE_IN_CH2 26
#define PIN_PULSE_IN_CH3 27

void pulse_counter_init(pcnt_unit_t unit, int pulse_gpio_num);
int16_t get_and_clear(pcnt_unit_t unit);

#endif
