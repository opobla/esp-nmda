#include "rmt_pulse_capture.h"
#include "pulse_monitor.h"
#include "common.h"
#include "datastructures.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_rx.h"
#include "driver/gpio.h"
#include <string.h>
#include <inttypes.h>

#ifdef CONFIG_ENABLE_RMT_PULSE_DETECTION

static const char *TAG = "RMT_PULSE_CAPTURE";

// RMT channel handles
static rmt_channel_handle_t rmt_channels[3] = {NULL, NULL, NULL};

// Event queue for processed events (deprecated, now using rmt_group_queue)
// static QueueHandle_t rmt_event_queue = NULL;

// Queue to notify callback completion (for restarting receive)
// We send channel index when callback completes
static QueueHandle_t rmt_callback_complete_queue = NULL;

// RMT receive buffers (one per channel)
// Each buffer can hold up to 64 symbols (as configured in mem_block_symbols)
#define RMT_RX_BUFFER_SIZE 64
static rmt_symbol_word_t rmt_rx_buffers[3][RMT_RX_BUFFER_SIZE];

// Structure to accumulate pulses from a single RMT callback (one group)
// Uses flexible array member for dynamic sizing
struct rmt_pulse_group {
    uint8_t channel_index;  // Internal channel index (0, 1, 2)
    uint8_t num_pulses;      // Number of pulses in this group
    int64_t start_timestamp; // Timestamp of first pulse (microseconds Unix)
    // Flexible array member: size determined by num_pulses
    rmt_pulse_t pulses[];  // Array of pulses (size = num_pulses)
};

// Queue for pulse group pointers (to avoid copying large structures)
static QueueHandle_t rmt_group_queue = NULL;

// Last event timestamp per channel (for separation calculation)
static int64_t last_event_timestamp[3] = {0, 0, 0};

// RMT receive callback - called from ISR context
// RMT RX captures symbols: each symbol represents a level change
// We process symbols to detect complete pulses (rising edge + falling edge)
static bool IRAM_ATTR rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    int channel_index = (int)(intptr_t)user_data;
    BaseType_t must_yield = pdFALSE;
    
    // Get current timestamp (microseconds)
    int64_t callback_time_us = esp_timer_get_time();
    
    // RMT resolution: 2MHz = 500ns per tick = 2 ticks per microsecond
    // Lower resolution allows longer symbol duration (max ~32.7ms vs ~819μs at 80MHz)
    const uint32_t ticks_per_us = 2;
    
    // Log callback invocation (from ISR, use ESP_EARLY_LOG)
    ESP_EARLY_LOGD(TAG, "RMT callback: channel %d, %u symbols", channel_index, edata->num_symbols);
    
    // Note: After this callback returns, we need to restart receiving
    // This will be done in the task_rmt_event_processor task
    
    if (edata->num_symbols > 0) {
        // First pass: count how many pulses we have
        uint8_t pulse_count = 0;
        for (uint32_t i = 0; i < edata->num_symbols; i++) {
            if (edata->received_symbols[i].level0 == 1 && edata->received_symbols[i].level1 == 0) {
                pulse_count++;
            }
        }
        
        // Allocate memory for group with exact number of pulses needed
        if (pulse_count == 0) {
            // No pulses in this batch, skip processing
            return false;
        }
        
        size_t group_size = sizeof(struct rmt_pulse_group) + (pulse_count * sizeof(rmt_pulse_t));
        struct rmt_pulse_group *group = (struct rmt_pulse_group*)heap_caps_malloc(
            group_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        
        if (group == NULL) {
            ESP_EARLY_LOGW(TAG, "Failed to allocate memory for pulse group: ch=%d, pulses=%d", 
                          channel_index, pulse_count);
            return false;
        }
        
        // Initialize group
        group->channel_index = channel_index;
        group->num_pulses = 0;
        group->start_timestamp = 0;
        
        // Calculate time: work backwards from callback time to find when first symbol started
        uint32_t total_duration_ticks = 0;
        for (uint32_t i = 0; i < edata->num_symbols; i++) {
            total_duration_ticks += edata->received_symbols[i].duration0 + edata->received_symbols[i].duration1;
        }
        uint32_t total_duration_us = total_duration_ticks / ticks_per_us;
        int64_t first_symbol_start = callback_time_us - total_duration_us;
        
        // Process symbols in forward order (oldest first) for correct separation calculation
        int64_t current_time = first_symbol_start;
        int64_t prev_pulse_end_time = 0;  // End time of previous pulse in this group
        
        for (uint32_t i = 0; i < edata->num_symbols; i++) {
            rmt_symbol_word_t symbol = edata->received_symbols[i];
            
            // Calculate symbol durations
            uint32_t symbol_duration0_us = symbol.duration0 / ticks_per_us;
            uint32_t symbol_duration1_us = symbol.duration1 / ticks_per_us;
            uint32_t symbol_total_duration_us = symbol_duration0_us + symbol_duration1_us;
            
            // Check for pulse: symbol with level0=HIGH(1) and level1=LOW(0)
            if (symbol.level0 == 1 && symbol.level1 == 0) {
                // This symbol represents a pulse
                int64_t pulse_start_time = current_time;
                uint32_t duration_us = symbol_duration0_us;
                int64_t pulse_end_time = pulse_start_time + duration_us + symbol_duration1_us;
                
                // Calculate separation from previous pulse
                int64_t separation_us = -1;
                if (group->num_pulses == 0) {
                    // First pulse in group: separation from last pulse of previous group
                    if (last_event_timestamp[channel_index] > 0) {
                        separation_us = pulse_start_time - last_event_timestamp[channel_index];
                    }
                } else {
                    // Subsequent pulse: separation from previous pulse in this group
                    separation_us = pulse_start_time - prev_pulse_end_time;
                }
                
                // Store pulse in group
                if (group->num_pulses < pulse_count) {  // Safety check
                    if (group->num_pulses == 0) {
                        group->start_timestamp = pulse_start_time;
                    }
                    group->pulses[group->num_pulses].duration_us = duration_us;
                    group->pulses[group->num_pulses].separation_us = separation_us;
                    group->num_pulses++;
                }
                
                // Update for next pulse calculation
                prev_pulse_end_time = pulse_end_time;
                last_event_timestamp[channel_index] = pulse_start_time;
            }
            
            // Move time forward
            current_time += symbol_total_duration_us;
        }
        
        // Send complete group to queue (from ISR)
        // Group is already allocated with exact size needed
        if (group->num_pulses > 0 && rmt_group_queue != NULL) {
            BaseType_t queue_result = xQueueSendFromISR(rmt_group_queue, &group, &must_yield);
            if (queue_result != pdTRUE) {
                ESP_EARLY_LOGW(TAG, "Failed to queue pulse group: ch=%d (queue full)", channel_index);
                heap_caps_free(group);  // Free if queue is full
            }
        } else {
            // No pulses or queue not available, free the group
            if (group != NULL) {
                heap_caps_free(group);
            }
        }
    }
    
    // Notify that callback completed (so we can restart receive)
    // Send channel index to callback complete queue
    if (rmt_callback_complete_queue != NULL) {
        uint8_t ch = (uint8_t)channel_index;
        BaseType_t yield_from_notify = pdFALSE;
        BaseType_t notify_result = xQueueSendFromISR(rmt_callback_complete_queue, &ch, &yield_from_notify);
        if (notify_result != pdTRUE) {
            ESP_EARLY_LOGD(TAG, "Failed to notify callback completion: ch=%d (queue full)", channel_index);
        }
        if (yield_from_notify == pdTRUE) {
            must_yield = pdTRUE;
        }
    }
    
    // Return whether we need to yield
    return (must_yield == pdTRUE);
}

esp_err_t rmt_pulse_capture_init(void)
{
    esp_err_t ret;
    
    // Create event queue
    rmt_group_queue = xQueueCreate(10, sizeof(struct rmt_pulse_group));
    if (rmt_group_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create RMT event queue");
        return ESP_ERR_NO_MEM;
    }
    
    // Create callback complete queue
    rmt_callback_complete_queue = xQueueCreate(10, sizeof(uint8_t));
    if (rmt_callback_complete_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create RMT callback complete queue");
        vQueueDelete(rmt_group_queue);
        rmt_group_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize last event timestamps
    for (int i = 0; i < 3; i++) {
        last_event_timestamp[i] = 0;
    }
    
    // GPIO pins for each channel
    int gpio_pins[3] = {PIN_PULSE_IN_CH1, PIN_PULSE_IN_CH2, PIN_PULSE_IN_CH3};
    
    // Configure RMT RX for each channel
    for (int i = 0; i < 3; i++) {
        // Configure RX channel
        rmt_rx_channel_config_t rx_channel_cfg = {
            .clk_src = RMT_CLK_SRC_DEFAULT,  // APB clock (typically 80MHz)
            .gpio_num = gpio_pins[i],
            .mem_block_symbols = 64,  // Memory block size
            .resolution_hz = 2000000,  // 2MHz = 500ns per tick (allows longer symbols)
        };
        
        ret = rmt_new_rx_channel(&rx_channel_cfg, &rmt_channels[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create RMT RX channel %d: %s", i, esp_err_to_name(ret));
            // Cleanup already created channels
            for (int j = 0; j < i; j++) {
                if (rmt_channels[j] != NULL) {
                    rmt_del_channel(rmt_channels[j]);
                    rmt_channels[j] = NULL;
                }
            }
            if (rmt_group_queue != NULL) {
                vQueueDelete(rmt_group_queue);
                rmt_group_queue = NULL;
            }
            return ret;
        }
        
        // Configure receive parameters
        // signal_range_max_ns: maximum pulse width to capture
        // Calculation: idle_reg_value = (resolution_hz * signal_range_max_ns) / 1e9
        // With resolution_hz = 2MHz and RMT_LL_MAX_IDLE_VALUE = 65535:
        // signal_range_max_ns <= 65535 * 1e9 / 2e6 = 32,767,500 ns ≈ 32.7 milliseconds
        // Using 10 milliseconds (10,000,000 ns) as a safe value well below the limit
        rmt_receive_config_t receive_cfg = {
            .signal_range_min_ns = CONFIG_RMT_GLITCH_FILTER_NS,
            .signal_range_max_ns = 10000000,  // 10 milliseconds max pulse width (10,000,000 ns)
        };
        
        ret = rmt_enable(rmt_channels[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable RMT channel %d: %s", i, esp_err_to_name(ret));
            goto cleanup;
        }
        
        // Register callback
        rmt_rx_event_callbacks_t cbs = {
            .on_recv_done = rmt_rx_done_callback,
        };
        
        ret = rmt_rx_register_event_callbacks(rmt_channels[i], &cbs, (void *)(intptr_t)i);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register RMT RX callback for channel %d: %s", i, esp_err_to_name(ret));
            goto cleanup;
        }
        
        // Start receiving with buffer
        // The buffer will be used by RMT to store received symbols
        // When the buffer is full or timeout occurs, the callback will be triggered
        ret = rmt_receive(rmt_channels[i], rmt_rx_buffers[i], sizeof(rmt_rx_buffers[i]), &receive_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start RMT receive on channel %d: %s", i, esp_err_to_name(ret));
            goto cleanup;
        }
        
        ESP_LOGI(TAG, "RMT channel %d initialized on GPIO %d", i, gpio_pins[i]);
    }
    
    ESP_LOGI(TAG, "RMT pulse capture initialized successfully");
    return ESP_OK;
    
cleanup:
    // Cleanup on error
    for (int i = 0; i < 3; i++) {
        if (rmt_channels[i] != NULL) {
            rmt_disable(rmt_channels[i]);
            rmt_del_channel(rmt_channels[i]);
            rmt_channels[i] = NULL;
        }
    }
    if (rmt_group_queue != NULL) {
        vQueueDelete(rmt_group_queue);
        rmt_group_queue = NULL;
    }
    if (rmt_callback_complete_queue != NULL) {
        vQueueDelete(rmt_callback_complete_queue);
        rmt_callback_complete_queue = NULL;
    }
    return ret;
}

esp_err_t rmt_pulse_capture_deinit(void)
{
    esp_err_t ret = ESP_OK;
    esp_err_t ret2;
    
    // Stop and delete all channels
    for (int i = 0; i < 3; i++) {
        if (rmt_channels[i] != NULL) {
            ret2 = rmt_disable(rmt_channels[i]);
            if (ret == ESP_OK) ret = ret2;
            
            ret2 = rmt_del_channel(rmt_channels[i]);
            if (ret == ESP_OK) ret = ret2;
            
            rmt_channels[i] = NULL;
        }
    }
    
    // Delete event queues
    if (rmt_group_queue != NULL) {
        vQueueDelete(rmt_group_queue);
        rmt_group_queue = NULL;
    }
    if (rmt_callback_complete_queue != NULL) {
        vQueueDelete(rmt_callback_complete_queue);
        rmt_callback_complete_queue = NULL;
    }
    
    // Clear last event timestamps
    for (int i = 0; i < 3; i++) {
        last_event_timestamp[i] = 0;
    }
    
    ESP_LOGI(TAG, "RMT pulse capture deinitialized");
    return ret;
}

QueueHandle_t rmt_pulse_capture_get_event_queue(void)
{
    return rmt_group_queue;
}

// Task to process RMT pulse groups and send to telemetry queue
// Also restarts RMT receive after each callback
void task_rmt_event_processor(void *parameters)
{
    struct rmt_pulse_group *group_ptr;
    struct telemetry_message message;
    rmt_receive_config_t receive_cfg = {
        .signal_range_min_ns = CONFIG_RMT_GLITCH_FILTER_NS,
        .signal_range_max_ns = 10000000,  // 10 milliseconds max pulse width (10,000,000 ns)
    };
    
    // Rate limiting for logging (max 3 messages per second)
    int64_t last_log_time[3] = {0, 0, 0};
    uint32_t log_count[3] = {0, 0, 0};
    const int64_t log_interval_us = 1000000 / 3;  // 333ms between logs (3 per second)
    
    ESP_LOGI(TAG, "RMT event processor task started on Core %d", xPortGetCoreID());
    
    if (rmt_group_queue == NULL || rmt_callback_complete_queue == NULL) {
        ESP_LOGE(TAG, "RMT queues not initialized");
        vTaskDelete(NULL);
        return;
    }
    
    while (true) {
        // Wait for a pulse group pointer (one message per RMT callback batch)
        if (xQueueReceive(rmt_group_queue, &group_ptr, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (group_ptr == NULL) {
                ESP_LOGW(TAG, "Received NULL group pointer");
                continue;
            }
            
            struct rmt_pulse_group *group = group_ptr;  // Use pointer for clarity
            
            // Extract pulses array from group and allocate separately
            // This allows us to free the group structure while keeping the pulses array alive
            size_t pulses_size = group->num_pulses * sizeof(rmt_pulse_t);
            rmt_pulse_t *pulses_array = (rmt_pulse_t*)heap_caps_malloc(
                pulses_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            
            if (pulses_array == NULL) {
                ESP_LOGW(TAG, "Failed to allocate memory for pulses array (%zu bytes)", pulses_size);
                heap_caps_free(group_ptr);
                continue;
            }
            
            // Copy pulses from group to separate allocation
            memcpy(pulses_array, group->pulses, pulses_size);
            
            // Prepare telemetry message
            message.tm_message_type = TM_RMT_PULSE_EVENT;
            message.timestamp = group->start_timestamp;
            
            // Convert channel index (0,1,2) to channel number (1,2,3)
            message.payload.tm_rmt_pulse_event.channel = group->channel_index + 1;  // ch1, ch2, ch3
            message.payload.tm_rmt_pulse_event.symbols = group->num_pulses;
            message.payload.tm_rmt_pulse_event.start_timestamp = group->start_timestamp;
            message.payload.tm_rmt_pulse_event.pulses = pulses_array;  // Assign separate allocation
            
            // Save values needed for logging before freeing the group
            uint8_t ch_idx = group->channel_index;
            uint8_t num_pulses = group->num_pulses;
            int64_t start_ts = group->start_timestamp;
            
            // Now we can free the group structure (pulses are copied to separate memory)
            heap_caps_free(group_ptr);
            
            // Send to telemetry queue
            if (xQueueSend(telemetry_queue, &message, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(TAG, "Failed to send RMT pulse group to telemetry queue (queue full)");
                // Free pulses array if queue is full
                heap_caps_free(pulses_array);
            } else {
                // Rate-limited logging (max 3 messages per second per channel)
                int64_t current_time = esp_timer_get_time();
                
                if (current_time - last_log_time[ch_idx] >= log_interval_us) {
                    // Reset counter and log
                    log_count[ch_idx] = 0;
                    last_log_time[ch_idx] = current_time;
                    
                    ESP_LOGI(TAG, "========================================");
                    ESP_LOGI(TAG, "RMT Pulse Group (ch%d):", message.payload.tm_rmt_pulse_event.channel);
                    ESP_LOGI(TAG, "  Symbols:      %u", num_pulses);
                    ESP_LOGI(TAG, "  Start time:   %lld us", start_ts);
                    ESP_LOGI(TAG, "  Timestamp:    %lld us", current_time);
                    ESP_LOGI(TAG, "========================================");
                } else {
                    // Increment counter (but don't log)
                    log_count[ch_idx]++;
                }
            }
            
        }
        
        // Check for callback completion notifications
        // The callback is called when buffer fills or timeout occurs
        // After callback, channel returns to RMT_FSM_ENABLE state and we can restart
        uint8_t completed_channel;
        while (xQueueReceive(rmt_callback_complete_queue, &completed_channel, 0) == pdTRUE) {
            // Small delay to ensure channel is back in enable state
            // The callback handler sets channel back to RMT_FSM_ENABLE state
            vTaskDelay(pdMS_TO_TICKS(1));
            
            // Restart receiving on this channel after callback completed
            if (rmt_channels[completed_channel] != NULL) {
                esp_err_t ret = rmt_receive(rmt_channels[completed_channel], 
                                           rmt_rx_buffers[completed_channel], 
                                           sizeof(rmt_rx_buffers[completed_channel]), 
                                           &receive_cfg);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to restart RMT receive on channel %d: %s (will retry on next callback)", 
                             completed_channel, esp_err_to_name(ret));
                    // Re-queue the notification to retry later
                    xQueueSend(rmt_callback_complete_queue, &completed_channel, 0);
                } else {
                    ESP_LOGD(TAG, "RMT receive restarted on channel %d", completed_channel);
                }
            }
        }
    }
}

#endif // CONFIG_ENABLE_RMT_PULSE_DETECTION


