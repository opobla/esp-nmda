#include "pulse_monitor.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#ifdef CONFIG_ENABLE_GPIO_PULSE_DETECTION

void IRAM_ATTR detection_isr_handler(void* arg) {
    struct telemetry_message message;
    message.payload.tm_detect.channel[0] = gpio_get_level(PIN_PULSE_IN_CH1);
    message.payload.tm_detect.channel[1] = gpio_get_level(PIN_PULSE_IN_CH2);
    message.payload.tm_detect.channel[2] = gpio_get_level(PIN_PULSE_IN_CH3);

    message.timestamp = esp_timer_get_time();

    message.tm_message_type = TM_PULSE_DETECTION;

    xQueueSendFromISR(telemetry_queue, &message, NULL);
}

void init_GPIO() {
    // Configure GPIO pins as inputs
    gpio_set_direction(PIN_PULSE_IN_CH1, GPIO_MODE_INPUT);
    gpio_set_direction(PIN_PULSE_IN_CH2, GPIO_MODE_INPUT);
    gpio_set_direction(PIN_PULSE_IN_CH3, GPIO_MODE_INPUT);

    // Configure interrupt type BEFORE installing ISR service
    gpio_set_intr_type(PIN_PULSE_IN_CH1, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(PIN_PULSE_IN_CH2, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(PIN_PULSE_IN_CH3, GPIO_INTR_ANYEDGE);

    // Install GPIO ISR service (ESP_INTR_FLAG_DEFAULT = 0)
    gpio_install_isr_service(0);

    // Add ISR handlers
    gpio_isr_handler_add(PIN_PULSE_IN_CH1, detection_isr_handler, NULL);
    gpio_isr_handler_add(PIN_PULSE_IN_CH2, detection_isr_handler, NULL);
    gpio_isr_handler_add(PIN_PULSE_IN_CH3, detection_isr_handler, NULL);
}

// Reconfigurar interrupciones GPIO después de que PCNT se inicialice
// (PCNT puede sobrescribir la configuración GPIO)
void reconfigure_GPIO_interrupts(void) {
    ESP_LOGI("PULSE_DETECTION", "Reconfiguring GPIO interrupts after PCNT initialization");
    
    // Reconfigurar tipo de interrupción (PCNT puede haberlo cambiado)
    gpio_set_intr_type(PIN_PULSE_IN_CH1, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(PIN_PULSE_IN_CH2, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(PIN_PULSE_IN_CH3, GPIO_INTR_ANYEDGE);
    
    // Asegurar que los handlers estén añadidos
    gpio_isr_handler_add(PIN_PULSE_IN_CH1, detection_isr_handler, NULL);
    gpio_isr_handler_add(PIN_PULSE_IN_CH2, detection_isr_handler, NULL);
    gpio_isr_handler_add(PIN_PULSE_IN_CH3, detection_isr_handler, NULL);
    
    ESP_LOGI("PULSE_DETECTION", "GPIO interrupts reconfigured");
}

#else // CONFIG_ENABLE_GPIO_PULSE_DETECTION

// Stub functions when GPIO pulse detection is disabled
void init_GPIO() {
    // Configure GPIO pins as inputs (for PCNT compatibility)
    gpio_set_direction(PIN_PULSE_IN_CH1, GPIO_MODE_INPUT);
    gpio_set_direction(PIN_PULSE_IN_CH2, GPIO_MODE_INPUT);
    gpio_set_direction(PIN_PULSE_IN_CH3, GPIO_MODE_INPUT);
}

void reconfigure_GPIO_interrupts(void) {
    // No-op when GPIO pulse detection is disabled
}

#endif // CONFIG_ENABLE_GPIO_PULSE_DETECTION
