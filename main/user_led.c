#include "user_led.h"

#ifdef CONFIG_ENABLE_USER_LED

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "USER_LED";

// Configuration from sdkconfig
#define USER_LED_GPIO CONFIG_USER_LED_GPIO
#define SHORT_DURATION_MS CONFIG_USER_LED_SHORT_DURATION_MS
#define LONG_DURATION_MS CONFIG_USER_LED_LONG_DURATION_MS
#define PAUSE_BETWEEN_BLINKS_MS CONFIG_USER_LED_PAUSE_BETWEEN_BLINKS_MS
#define PAUSE_BETWEEN_CYCLES_MS CONFIG_USER_LED_PAUSE_BETWEEN_CYCLES_MS

// Queue for receiving condition change commands
static QueueHandle_t user_led_queue = NULL;
static TaskHandle_t user_led_task_handle = NULL;

// Current condition
static user_led_condition_t current_condition = USER_LED_OFF;

/**
 * @brief Pattern definitions for each condition
 * 
 * Each pattern is an array of durations in milliseconds.
 * Positive values = LED ON, Negative values = LED OFF
 * Pattern ends with 0
 */
typedef struct {
    int16_t *pattern;
    uint8_t pattern_length;
} led_pattern_t;

// Pattern storage arrays
static int16_t pattern_booting[] = {
    SHORT_DURATION_MS, -PAUSE_BETWEEN_BLINKS_MS,
    SHORT_DURATION_MS, -PAUSE_BETWEEN_BLINKS_MS,
    LONG_DURATION_MS, -PAUSE_BETWEEN_BLINKS_MS,
    LONG_DURATION_MS, 0
};

static int16_t pattern_wifi_connecting[] = {
    SHORT_DURATION_MS, -PAUSE_BETWEEN_BLINKS_MS,
    SHORT_DURATION_MS, 0
};

static int16_t pattern_wifi_error[] = {
    SHORT_DURATION_MS, -PAUSE_BETWEEN_BLINKS_MS,
    SHORT_DURATION_MS, -PAUSE_BETWEEN_BLINKS_MS,
    SHORT_DURATION_MS, -PAUSE_BETWEEN_BLINKS_MS,
    LONG_DURATION_MS, 0
};

static int16_t pattern_ntp_connecting[] = {
    SHORT_DURATION_MS, -PAUSE_BETWEEN_BLINKS_MS,
    LONG_DURATION_MS, 0
};

static int16_t pattern_ntp_error[] = {
    SHORT_DURATION_MS, -PAUSE_BETWEEN_BLINKS_MS,
    LONG_DURATION_MS, -PAUSE_BETWEEN_BLINKS_MS,
    LONG_DURATION_MS, 0
};

static int16_t pattern_data_acquisition[] = {
    SHORT_DURATION_MS, -PAUSE_BETWEEN_BLINKS_MS, 0
};

// Pattern lookup table
// Note: pattern_length includes the terminating 0, but we iterate until we find 0
static const led_pattern_t patterns[] = {
    [USER_LED_BOOTING] = {pattern_booting, sizeof(pattern_booting) / sizeof(int16_t)},
    [USER_LED_WIFI_CONNECTING] = {pattern_wifi_connecting, sizeof(pattern_wifi_connecting) / sizeof(int16_t)},
    [USER_LED_WIFI_ERROR] = {pattern_wifi_error, sizeof(pattern_wifi_error) / sizeof(int16_t)},
    [USER_LED_NTP_CONNECTING] = {pattern_ntp_connecting, sizeof(pattern_ntp_connecting) / sizeof(int16_t)},
    [USER_LED_NTP_ERROR] = {pattern_ntp_error, sizeof(pattern_ntp_error) / sizeof(int16_t)},
    [USER_LED_DATA_ACQUISITION] = {pattern_data_acquisition, sizeof(pattern_data_acquisition) / sizeof(int16_t)},
    [USER_LED_OFF] = {NULL, 0}
};

/**
 * @brief User LED task that executes blink patterns
 */
static void user_led_task(void *pvParameters) {
    user_led_condition_t new_condition;
    
    ESP_LOGI(TAG, "User LED task started on core %d", xPortGetCoreID());
    
    while (1) {
        // Check for new condition commands (non-blocking with timeout)
        if (xQueueReceive(user_led_queue, &new_condition, pdMS_TO_TICKS(10)) == pdTRUE) {
            current_condition = new_condition;
            ESP_LOGI(TAG, "Condition changed to: %d", current_condition);
        }
        
        // Execute current pattern
        if (current_condition == USER_LED_OFF) {
            gpio_set_level(USER_LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(100)); // Small delay when off
            continue;
        }
        
        const led_pattern_t *pattern = &patterns[current_condition];
        if (pattern->pattern == NULL || pattern->pattern_length == 0) {
            gpio_set_level(USER_LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Execute pattern cycle
        for (uint8_t i = 0; i < pattern->pattern_length; i++) {
            int16_t duration = pattern->pattern[i];
            
            if (duration == 0) {
                break; // End of pattern
            }
            
            if (duration > 0) {
                // LED ON
                gpio_set_level(USER_LED_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(duration));
            } else {
                // LED OFF (pause)
                gpio_set_level(USER_LED_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(-duration));
            }
        }
        
        // Pause between cycles
        gpio_set_level(USER_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(PAUSE_BETWEEN_CYCLES_MS));
    }
}

esp_err_t user_led_init(void) {
    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << USER_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", USER_LED_GPIO, esp_err_to_name(ret));
        return ret;
    }
    
    // Set LED off initially
    gpio_set_level(USER_LED_GPIO, 0);
    
    // Create queue for condition commands
    user_led_queue = xQueueCreate(5, sizeof(user_led_condition_t));
    if (user_led_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_ERR_NO_MEM;
    }
    
    // Create task
    BaseType_t task_ret = xTaskCreate(
        user_led_task,
        "user_led_task",
        2048,  // Stack size
        NULL,
        3,     // Priority (medium)
        &user_led_task_handle
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create user LED task");
        vQueueDelete(user_led_queue);
        user_led_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "User LED initialized on GPIO %d", USER_LED_GPIO);
    ESP_LOGI(TAG, "Timings: short=%dms, long=%dms, pause=%dms, cycle_pause=%dms",
             SHORT_DURATION_MS, LONG_DURATION_MS, PAUSE_BETWEEN_BLINKS_MS, PAUSE_BETWEEN_CYCLES_MS);
    
    return ESP_OK;
}

esp_err_t user_led_set_condition(user_led_condition_t condition) {
    if (user_led_queue == NULL) {
        ESP_LOGW(TAG, "User LED not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (condition >= USER_LED_OFF + 1) {
        ESP_LOGE(TAG, "Invalid condition: %d", condition);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xQueueSend(user_led_queue, &condition, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to send condition to queue");
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

esp_err_t user_led_off(void) {
    return user_led_set_condition(USER_LED_OFF);
}

#endif // CONFIG_ENABLE_USER_LED

