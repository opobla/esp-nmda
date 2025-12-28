#include "spl06.h"

#ifdef CONFIG_ENABLE_SPL06

#include "common.h"
#include "datastructures.h"
#include "esp32-libs.h"
#include "sdkconfig.h"
#include <sys/time.h>

/**
 * @brief Calculate QNH (pressure reduced to sea level) using AEMET formula
 * 
 * Formula: QNH = P + (H/27)
 * Where:
 *   - QNH: Pressure at sea level in hPa
 *   - P: Absolute pressure measured at station altitude in hPa
 *   - H: Station altitude above sea level in meters
 * 
 * @param pressure_hpa Absolute pressure in hPa
 * @param altitude_m Station altitude in meters
 * @return QNH in hPa
 */
static float calculate_qnh_aemet(float pressure_hpa, int altitude_m)
{
    return pressure_hpa + ((float)altitude_m / 27.0f);
}

void spl06_monitor_task(void *parameters)
{
    ESP_LOGI("SPL06_MONITOR", "Starting on Core %d", xPortGetCoreID());

    struct telemetry_message message;
    message.tm_message_type = TM_SPL06;

    float pressure_pa = 0.0f;
    float temperature_celsius = 0.0f;
    struct timeval tv_now;
    
    // Get station altitude from configuration
    int station_altitude_m = CONFIG_SPL06_STATION_ALTITUDE_M;
    
    // Get publish period from configuration (in seconds)
    int publish_period_sec = CONFIG_SPL06_PUBLISH_PERIOD_SEC;

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ms = (TickType_t)(publish_period_sec * 1000);

    while (true) {
        esp_err_t ret = spl06_read_both(&pressure_pa, &temperature_celsius);
        
        if (ret == ESP_OK) {
            // Get timestamp
            gettimeofday(&tv_now, NULL);
            int64_t time_us = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
            message.timestamp = time_us;

            // Fill message payload
            message.payload.tm_spl06.pressure_pa = pressure_pa;
            message.payload.tm_spl06.pressure_hpa = pressure_pa / 100.0f;  // Convert to hPa
            message.payload.tm_spl06.temperature_celsius = temperature_celsius;
            
            // Calculate QNH using AEMET formula: QNH = P + (H/27)
            message.payload.tm_spl06.qnh_hpa = calculate_qnh_aemet(
                message.payload.tm_spl06.pressure_hpa, 
                station_altitude_m
            );

            // Print values to console
            ESP_LOGI("SPL06_MONITOR", "========================================");
            ESP_LOGI("SPL06_MONITOR", "SPL06 Sensor Reading:");
            ESP_LOGI("SPL06_MONITOR", "  Pressure:     %.2f Pa", pressure_pa);
            ESP_LOGI("SPL06_MONITOR", "  Pressure:     %.2f hPa", message.payload.tm_spl06.pressure_hpa);
            ESP_LOGI("SPL06_MONITOR", "  Temperature:  %.2f °C", temperature_celsius);
            ESP_LOGI("SPL06_MONITOR", "  QNH:          %.2f hPa (altitude: %d m)", 
                     message.payload.tm_spl06.qnh_hpa, station_altitude_m);
            ESP_LOGI("SPL06_MONITOR", "  Timestamp:    %lld us", time_us);
            ESP_LOGI("SPL06_MONITOR", "========================================");

            // Send to telemetry queue
            if (telemetry_queue == NULL) {
                ESP_LOGE("SPL06_MONITOR", "Telemetry queue is NULL!");
            } else if (xQueueSend(telemetry_queue, &message, 0) != pdTRUE) {
                ESP_LOGW("SPL06_MONITOR", "Failed to send message to telemetry queue");
            } else {
                ESP_LOGI("SPL06_MONITOR", "Message sent to telemetry queue");
            }
        } else {
            ESP_LOGE("SPL06_MONITOR", "Failed to read SPL06: %s", esp_err_to_name(ret));
        }

        // Wait for next period
        vTaskDelayUntil(&last_wake_time, period_ms);
    }
}

#endif // CONFIG_ENABLE_SPL06

