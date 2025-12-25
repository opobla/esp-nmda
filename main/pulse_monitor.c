#include "pulse_monitor.h"
#include <sys/time.h>
#include <time.h>
#include <inttypes.h>



void pulse_counter_init(pcnt_unit_t unit, int pulse_gpio_num) {
    /* Prepare configuration for the PCNT unit */
    pcnt_config_t pcnt_config = {
        // Set PCNT input signal and control GPIOs
        .pulse_gpio_num = pulse_gpio_num,
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .channel = PCNT_CHANNEL_0,
        .unit = unit,
        // What to do on the positive / negative edge of pulse input?
        .pos_mode = PCNT_COUNT_DIS,   // Disable counting on positive edge
        .neg_mode = PCNT_COUNT_INC,   // Increment counter on negative edge
        // What to do when control input is low or high?
        .lctrl_mode = PCNT_MODE_KEEP, // Keep counting direction if low
        .hctrl_mode = PCNT_MODE_KEEP, // Keep the primary counter mode if high
    };

    /* Initialize PCNT unit */
    pcnt_unit_config(&pcnt_config);

    /* Configure and enable the input filter */
    /* Units are in APB_CLOCK 80MHz ticks. Each tick is approximately 13ns */
    /* Filter value of 100 ticks = ~1.3μs filter window */
    pcnt_set_filter_value(unit, 100);
    pcnt_filter_enable(unit);

    /* Initialize PCNT's counter */
    pcnt_counter_pause(unit);
    pcnt_counter_clear(unit);

    /* Everything is set up, now go to counting */
    pcnt_counter_resume(unit);
}

int16_t get_and_clear(pcnt_unit_t unit) {
    int16_t count = 0;
    pcnt_get_counter_value(unit, &count);
    pcnt_counter_clear(unit);
    return count;
}

void task_pcnt(void *parameters) {
    const int32_t count_time_secs = 10;
    int32_t count[3] = { 0 };
    struct timeval tv_now;
    struct telemetry_message message;

    ESP_LOGI("TASK_PCNT", "Starting on %d Core", xPortGetCoreID());

    // Initialize all three PCNT units
    pulse_counter_init(PCNT_UNIT_0, PIN_PULSE_IN_CH1);
    pulse_counter_init(PCNT_UNIT_1, PIN_PULSE_IN_CH2);
    pulse_counter_init(PCNT_UNIT_2, PIN_PULSE_IN_CH3);

    message.tm_message_type = TM_PULSE_COUNT;

    while (true) {
        // Read and clear counters for all channels
        count[0] = get_and_clear(PCNT_UNIT_0);
        count[1] = get_and_clear(PCNT_UNIT_1);
        count[2] = get_and_clear(PCNT_UNIT_2);

        ESP_LOGI("MONITOR", "CH1: %d pulses per %ld secs", (int)count[0], (long)count_time_secs);
        ESP_LOGI("MONITOR", "CH2: %d pulses per %ld secs", (int)count[1], (long)count_time_secs);
        ESP_LOGI("MONITOR", "CH3: %d pulses per %ld secs", (int)count[2], (long)count_time_secs);

        // Prepare telemetry message
        message.payload.tm_pcnt.integration_time_sec = count_time_secs;
        message.payload.tm_pcnt.channel[0] = (uint32_t)count[0];
        message.payload.tm_pcnt.channel[1] = (uint32_t)count[1];
        message.payload.tm_pcnt.channel[2] = (uint32_t)count[2];

        // Get current timestamp
        gettimeofday(&tv_now, NULL);
        message.timestamp = (int64_t)tv_now.tv_sec * 1000000LL + (int64_t)tv_now.tv_usec;
        
        // Send telemetry message
        xQueueSend(telemetry_queue, &message, portMAX_DELAY);

        // Calculate sleep time to align with 10-second intervals
        time_t now;
        time(&now);
        int64_t time_to_sleep_secs = count_time_secs - (now % count_time_secs);
        if (time_to_sleep_secs <= 0) {
            time_to_sleep_secs = count_time_secs;
        }

        ESP_LOGI("MONITOR", "Sleeping for %lld s", (long long)time_to_sleep_secs);
        vTaskDelay(pdMS_TO_TICKS(time_to_sleep_secs * 1000));
    }
}
