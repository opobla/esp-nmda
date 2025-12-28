#ifndef __PULSE_COINCIDENCE_H_
#define __PULSE_COINCIDENCE_H_

#include "esp_err.h"
#include "rmt_pulse_capture.h"
#include "datastructures.h"

#ifdef CONFIG_ENABLE_RMT_PULSE_DETECTION

/**
 * @brief Inicializar detector de coincidencias y multiplicidades
 * 
 * @return esp_err_t ESP_OK si la inicialización fue exitosa
 */
esp_err_t coincidence_detector_init(void);

/**
 * @brief Desinicializar detector de coincidencias
 * 
 * @return esp_err_t ESP_OK si la desinicialización fue exitosa
 */
esp_err_t coincidence_detector_deinit(void);

/**
 * @brief Procesar un nuevo evento de pulso y detectar coincidencias/multiplicidades
 * 
 * @param event Evento de pulso a procesar
 * @return esp_err_t ESP_OK si el procesamiento fue exitoso
 */
esp_err_t coincidence_detector_process_event(const struct rmt_pulse_event *event);

/**
 * @brief Obtener estadísticas de coincidencias detectadas
 * 
 * @param coinc_2_ch01 Contador de coincidencias 2 entre canal 0 y 1
 * @param coinc_2_ch12 Contador de coincidencias 2 entre canal 1 y 2
 * @param coinc_2_ch02 Contador de coincidencias 2 entre canal 0 y 2
 * @param coinc_3 Contador de coincidencias 3
 * @return esp_err_t ESP_OK si se obtuvieron las estadísticas correctamente
 */
esp_err_t coincidence_detector_get_stats(uint32_t *coinc_2_ch01, uint32_t *coinc_2_ch12, 
                                        uint32_t *coinc_2_ch02, uint32_t *coinc_3);

/**
 * @brief Obtener estadísticas de multiplicidades detectadas
 * 
 * @param multiplicity_count Array de 3 elementos con contadores por canal
 * @return esp_err_t ESP_OK si se obtuvieron las estadísticas correctamente
 */
esp_err_t multiplicity_detector_get_stats(uint32_t multiplicity_count[3]);

#endif // CONFIG_ENABLE_RMT_PULSE_DETECTION

#endif // __PULSE_COINCIDENCE_H_

