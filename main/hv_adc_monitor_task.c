#include "hv_adc.h"

#ifdef CONFIG_ENABLE_HV_SUPPORT

#include "common.h"
#include "esp32-libs.h"
#include <sys/time.h>

#define HV_ADC_MONITOR_RATE_HZ 1.0f  // Default: 1 Hz (1 lectura por segundo)

void hv_adc_monitor_task(void *parameters)
{
    ESP_LOGI("HV_ADC_MONITOR", "Starting on Core %d", xPortGetCoreID());

    float channel_voltages[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float temperature_celsius = 0.0f;
    struct timeval tv_now;

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ms = (TickType_t)(1000.0f / HV_ADC_MONITOR_RATE_HZ);

    while (true) {
        // Read all 4 channels
        for (uint8_t ch = 0; ch < 4; ch++) {
            esp_err_t ret = hv_adc_read_channel(ch, &channel_voltages[ch]);
            if (ret != ESP_OK) {
                ESP_LOGW("HV_ADC_MONITOR", "Failed to read channel %d: %s", ch, esp_err_to_name(ret));
            }
        }

        // Read internal temperature
        esp_err_t temp_ret = hv_adc_read_temperature(&temperature_celsius);

        // Get timestamp
        gettimeofday(&tv_now, NULL);
        int64_t time_us = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;

        // Print values to console with semantic names
        ESP_LOGI("HV_ADC_MONITOR", "========================================");
        ESP_LOGI("HV_ADC_MONITOR", "HV ADC Readings (ADS112C04):");
        ESP_LOGI("HV_ADC_MONITOR", "  HV_Vmon (ch0): %.2f mV", channel_voltages[0]);
        ESP_LOGI("HV_ADC_MONITOR", "  HV_Vset (ch1): %.2f mV", channel_voltages[1]);
        ESP_LOGI("HV_ADC_MONITOR", "  HV_Isense (ch2): %.2f mV", channel_voltages[2]);
        ESP_LOGI("HV_ADC_MONITOR", "  Channel 3:    %.2f mV", channel_voltages[3]);
        if (temp_ret == ESP_OK) {
            ESP_LOGI("HV_ADC_MONITOR", "  Temperature:   %.2f °C", temperature_celsius);
        } else {
            ESP_LOGW("HV_ADC_MONITOR", "  Temperature:   Failed to read (%s)", esp_err_to_name(temp_ret));
        }
        ESP_LOGI("HV_ADC_MONITOR", "  Timestamp:    %lld us", time_us);
        ESP_LOGI("HV_ADC_MONITOR", "========================================");

        // Wait for next period
        vTaskDelayUntil(&last_wake_time, period_ms);
    }
}

#endif // CONFIG_ENABLE_HV_SUPPORT

