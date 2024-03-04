#ifndef METEO_BMP280_H
#define METEO_BMP280_H


#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "bmp280.h"
#include "bmp2_defs.h"


#define SPI_MISO_PIN GPIO_NUM_19
#define SPI_MOSI_PIN GPIO_NUM_23
#define SPI_CLK_PIN GPIO_NUM_18
#define SPI_CS_PIN GPIO_NUM_21


static const char *TAG = "BMP280";

#define DMA_CHAN 1

int8_t initialize_spi_communication();
int8_t initialize_spi_sensor();
int bmp280_task(void *parameters);
static int8_t get_data(uint32_t period, struct bmp2_dev *dev);
BMP2_INTF_RET_TYPE bmp240_read  (uint8_t reg_addr, uint8_t *reg_data, uint32_t length, const void *intf_ptr);
BMP2_INTF_RET_TYPE bmp240_write (uint8_t reg_addr, const uint8_t *reg_data, uint32_t length, const void *intf_ptr);
void delay_us(uint32_t period_us, void* intf_ptr);
void bmp2_error_codes_print_result(const char api_name[], int8_t rslt);


#endif