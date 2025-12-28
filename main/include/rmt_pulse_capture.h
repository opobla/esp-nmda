#ifndef __RMT_PULSE_CAPTURE_H_
#define __RMT_PULSE_CAPTURE_H_

#include "esp_err.h"
#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef CONFIG_ENABLE_RMT_PULSE_DETECTION

// Estructura para eventos de pulso capturados por RMT
struct rmt_pulse_event {
    uint8_t channel;           // Canal (0, 1, o 2)
    int64_t timestamp_us;      // Timestamp de inicio del pulso (microsegundos)
    uint32_t duration_us;      // Duración del pulso (microsegundos)
    int64_t separation_us;     // Separación con pulso anterior (microsegundos, -1 si es el primero)
    uint8_t edge_type;         // Tipo de flanco: 0=rising, 1=falling
};

// Tipos de coincidencia
#define COINC_2_CH01 0x01  // Coincidencia entre canal 0 y 1
#define COINC_2_CH12 0x02  // Coincidencia entre canal 1 y 2
#define COINC_2_CH02 0x03  // Coincidencia entre canal 0 y 2
#define COINC_3      0x04  // Coincidencia entre los 3 canales

/**
 * @brief Inicializar captura RMT para los 3 canales de pulsos
 * 
 * @return esp_err_t ESP_OK si la inicialización fue exitosa
 */
esp_err_t rmt_pulse_capture_init(void);

/**
 * @brief Desinicializar captura RMT y liberar recursos
 * 
 * @return esp_err_t ESP_OK si la desinicialización fue exitosa
 */
esp_err_t rmt_pulse_capture_deinit(void);

/**
 * @brief Obtener la cola de eventos RMT para procesamiento
 * 
 * @return QueueHandle_t Handle de la cola de eventos, NULL si no está inicializado
 */
QueueHandle_t rmt_pulse_capture_get_event_queue(void);

/**
 * @brief Tarea de procesamiento de eventos RMT
 * 
 * Esta tarea procesa eventos de la cola RMT y los envía a la cola de telemetría
 * 
 * @param parameters Parámetros de la tarea (no usado)
 */
void task_rmt_event_processor(void *parameters);

#endif // CONFIG_ENABLE_RMT_PULSE_DETECTION

#endif // __RMT_PULSE_CAPTURE_H_

