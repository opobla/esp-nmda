#ifndef __USER_LED_H__
#define __USER_LED_H__

#include "esp_err.h"

#ifdef CONFIG_ENABLE_USER_LED

/**
 * @brief System conditions that can be indicated via LED patterns
 */
typedef enum {
    USER_LED_BOOTING,           // 2 cortos + 2 largos
    USER_LED_WIFI_CONNECTING,  // 2 cortos
    USER_LED_WIFI_ERROR,        // 3 cortos + 1 largo
    USER_LED_NTP_CONNECTING,    // 1 corto + 1 largo
    USER_LED_NTP_ERROR,         // 1 corto + 2 largos
    USER_LED_DATA_ACQUISITION,  // 1 corto (parpadeo continuo)
    USER_LED_OFF                // LED apagado
} user_led_condition_t;

/**
 * @brief Initialize the user LED module
 * 
 * This function should be called early in app_main() to enable
 * system status indication via LED patterns.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t user_led_init(void);

/**
 * @brief Set the system condition to indicate via LED pattern
 * 
 * The LED will continuously repeat the pattern for the given condition
 * until a new condition is set.
 * 
 * @param condition The system condition to indicate
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t user_led_set_condition(user_led_condition_t condition);

/**
 * @brief Turn off the LED completely
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t user_led_off(void);

#else // CONFIG_ENABLE_USER_LED

// Stub functions when user LED is disabled
#define user_led_init() ESP_OK
#define user_led_set_condition(condition) ESP_OK
#define user_led_off() ESP_OK

#endif // CONFIG_ENABLE_USER_LED

#endif // __USER_LED_H__

