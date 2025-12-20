#ifndef __HV_ADC_H_
#define __HV_ADC_H_

#include "esp_err.h"
#include "sdkconfig.h"
#include <stdbool.h>

#ifdef CONFIG_ENABLE_HV_SUPPORT

// ADS112C04 I2C address (default)
#define HV_ADC_I2C_ADDR_DEFAULT  0x48

// ADS112C04 Commands (see datasheet section 8.5.3)
#define HV_ADC_CMD_RESET    0x06  // Reset command: 0000 011x
#define HV_ADC_CMD_START    0x08  // START/SYNC command: 0000 100x
#define HV_ADC_CMD_POWERDOWN 0x02 // POWERDOWN command: 0000 001x
#define HV_ADC_CMD_RDATA    0x10  // RDATA command: 0001 xxxx
#define HV_ADC_CMD_RREG     0x20  // RREG command: 0010 nnxx (nn=register, xx=count-1)
#define HV_ADC_CMD_WREG     0x40  // WREG command: 0100 nnxx (nn=register, xx=count-1)

// ADS112C04 Register addresses (used with RREG/WREG commands)
#define HV_ADC_REG_CONFIG0       0x00
#define HV_ADC_REG_CONFIG1       0x01
#define HV_ADC_REG_CONFIG2       0x02
#define HV_ADC_REG_CONFIG3       0x03
#define HV_ADC_REG_LO_THRESH     0x04
#define HV_ADC_REG_HI_THRESH     0x05
#define HV_ADC_REG_DATA          0x06  // Read-only: conversion result
#define HV_ADC_REG_TEMP          0x07  // Read-only: temperature sensor

// Configuration register 0 bits
#define HV_ADC_CONFIG0_MUX_MASK          0xF0
#define HV_ADC_CONFIG0_MUX_SHIFT         4
#define HV_ADC_CONFIG0_GAIN_MASK         0x0E
#define HV_ADC_CONFIG0_GAIN_SHIFT        1
#define HV_ADC_CONFIG0_PGA_BYPASS        (1 << 0)

// MUX settings (differential pairs)
#define HV_ADC_MUX_AIN0_AIN1     0x00
#define HV_ADC_MUX_AIN0_AIN2     0x01
#define HV_ADC_MUX_AIN0_AIN3     0x02
#define HV_ADC_MUX_AIN1_AIN2     0x03
#define HV_ADC_MUX_AIN1_AIN3     0x04
#define HV_ADC_MUX_AIN2_AIN3     0x05
#define HV_ADC_MUX_AIN1_AIN0     0x06
#define HV_ADC_MUX_AIN3_AIN2     0x07
#define HV_ADC_MUX_AIN0_AVSS     0x08  // Single-ended
#define HV_ADC_MUX_AIN1_AVSS     0x09
#define HV_ADC_MUX_AIN2_AVSS     0x0A
#define HV_ADC_MUX_AIN3_AVSS     0x0B
#define HV_ADC_MUX_TEMP           0x0C  // Internal temperature sensor
#define HV_ADC_MUX_AVDD_AVSS      0x0D  // Supply voltage monitor

// Gain settings
#define HV_ADC_GAIN_1             0x00
#define HV_ADC_GAIN_2             0x01
#define HV_ADC_GAIN_4             0x02
#define HV_ADC_GAIN_8             0x03
#define HV_ADC_GAIN_16            0x04
#define HV_ADC_GAIN_32            0x05
#define HV_ADC_GAIN_64            0x06
#define HV_ADC_GAIN_128           0x07

// Configuration register 1 bits
#define HV_ADC_CONFIG1_DR_MASK    0xE0
#define HV_ADC_CONFIG1_DR_SHIFT    5
#define HV_ADC_CONFIG1_CM_MASK    0x18
#define HV_ADC_CONFIG1_CM_SHIFT   3
#define HV_ADC_CONFIG1_BCS         (1 << 2)  // Burnout current source
#define HV_ADC_CONFIG1_VREF_MASK   0x03

// Data rate settings
#define HV_ADC_DR_20SPS           0x00
#define HV_ADC_DR_45SPS           0x01
#define HV_ADC_DR_90SPS           0x02
#define HV_ADC_DR_175SPS          0x03
#define HV_ADC_DR_330SPS          0x04
#define HV_ADC_DR_600SPS          0x05
#define HV_ADC_DR_1000SPS         0x06

// Conversion mode
#define HV_ADC_CM_SINGLE          0x00  // Single-shot
#define HV_ADC_CM_CONTINUOUS      0x01  // Continuous conversion
#define HV_ADC_CM_PULSE           0x02  // Pulse conversion

// VREF settings
#define HV_ADC_VREF_INTERNAL      0x00  // Internal 2.048V
#define HV_ADC_VREF_EXTERNAL      0x01  // External reference
#define HV_ADC_VREF_AVDD_AVSS     0x02  // AVDD-AVSS
#define HV_ADC_VREF_ANALOG        0x03  // Analog supply

// Configuration register 2 bits
#define HV_ADC_CONFIG2_DRDY       (1 << 7)  // Data ready flag (read-only)
#define HV_ADC_CONFIG2_DCNT       (1 << 6)  // Data counter enable
#define HV_ADC_CONFIG2_CRC_MASK   0x30
#define HV_ADC_CONFIG2_CRC_SHIFT  4
#define HV_ADC_CONFIG2_BCS        (1 << 3)  // Burn-out current sources
#define HV_ADC_CONFIG2_IDAC_MASK  0x07
#define HV_ADC_CONFIG2_IDAC_SHIFT 0

// Configuration register 3 bits
#define HV_ADC_CONFIG3_I1MUX_MASK 0xE0
#define HV_ADC_CONFIG3_I1MUX_SHIFT 5
#define HV_ADC_CONFIG3_I2MUX_MASK 0x1C
#define HV_ADC_CONFIG3_I2MUX_SHIFT 2
#define HV_ADC_CONFIG3_RESERVED   0x03

/**
 * @brief Initialize the HV ADC (ADS112C04)
 * 
 * Initializes the ADC with default configuration:
 * - Single-shot conversion mode
 * - 20 SPS data rate
 * - Gain = 1
 * - Internal 2.048V reference
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t hv_adc_init(void);

/**
 * @brief Read ADC channel (differential or single-ended)
 * 
 * @param channel Channel to read (0-3 for single-ended, or use MUX defines for differential)
 * @param voltage_mv Pointer to store voltage in millivolts
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t hv_adc_read_channel(uint8_t channel, float *voltage_mv);

/**
 * @brief Read differential channel
 * 
 * @param mux_config MUX configuration (use HV_ADC_MUX_* defines)
 * @param voltage_mv Pointer to store voltage in millivolts
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t hv_adc_read_differential(uint8_t mux_config, float *voltage_mv);

/**
 * @brief Read internal temperature sensor
 * 
 * @param temp_celsius Pointer to store temperature in Celsius
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t hv_adc_read_temperature(float *temp_celsius);

/**
 * @brief Configure ADC gain
 * 
 * @param gain Gain setting (use HV_ADC_GAIN_* defines)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t hv_adc_set_gain(uint8_t gain);

/**
 * @brief Configure ADC data rate
 * 
 * @param data_rate Data rate setting (use HV_ADC_DR_* defines)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t hv_adc_set_data_rate(uint8_t data_rate);

/**
 * @brief Start a single conversion
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t hv_adc_start_conversion(void);

/**
 * @brief Check if conversion is ready
 * 
 * @return true if ready, false otherwise
 */
bool hv_adc_is_ready(void);

/**
 * @brief Read conversion result
 * 
 * @param raw_value Pointer to store raw 16-bit value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t hv_adc_read_result(int16_t *raw_value);

#endif // CONFIG_ENABLE_HV_SUPPORT

#endif // __HV_ADC_H_

