#include "pulse_monitor.h"
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"

static const char *TAG = "PULSE_MONITOR";

// Handles del nuevo driver pulse_cnt
static pcnt_unit_handle_t pcnt_units[3] = {NULL, NULL, NULL};
static pcnt_channel_handle_t pcnt_channels[3] = {NULL, NULL, NULL};

// Calcular el próximo segundo alineado (10, 20, 30, 40, 50, 0)
static int calculate_next_aligned_second(time_t current_time) {
    int current_second = current_time % 60;
    
    if (current_second < 10) {
        return 10;
    } else if (current_second < 20) {
        return 20;
    } else if (current_second < 30) {
        return 30;
    } else if (current_second < 40) {
        return 40;
    } else if (current_second < 50) {
        return 50;
    } else {
        return 60; // Próximo minuto (segundo 0)
    }
}

// Calcular el tiempo de espera hasta el siguiente segundo alineado en milisegundos
// Retorna el tiempo en milisegundos y actualiza next_aligned con el segundo objetivo
static int64_t calculate_wait_time_to_aligned_second(int *next_aligned) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    time_t now = tv.tv_sec;
    int current_second = now % 60;
    int target_second = calculate_next_aligned_second(now);
    
    if (next_aligned != NULL) {
        *next_aligned = target_second;
    }
    
    // Calcular segundos hasta el siguiente segundo alineado
    int seconds_to_wait = target_second - current_second;
    
    if (seconds_to_wait == 0) {
        // Ya estamos en un segundo alineado, esperar al siguiente intervalo (10 segundos)
        seconds_to_wait = 10;
        if (next_aligned != NULL) {
            // El siguiente segundo alineado será 10 segundos después
            *next_aligned = (current_second + 10) % 60;
        }
    }
    
    // Calcular tiempo total en milisegundos: segundos + milisegundos restantes del segundo actual
    int64_t wait_ms = (int64_t)seconds_to_wait * 1000LL;
    
    // Ajustar para esperar hasta el inicio exacto del siguiente segundo alineado
    // Si estamos en el segundo X con Y microsegundos, esperamos hasta el segundo X+seconds_to_wait con 0 microsegundos
    // Convertir microsegundos a milisegundos (redondeando hacia arriba para mayor precisión)
    int64_t remaining_ms = (int64_t)tv.tv_usec / 1000LL;
    wait_ms -= remaining_ms;
    
    // Asegurar que el tiempo de espera sea positivo
    if (wait_ms < 0) {
        wait_ms += 1000LL; // Añadir 1 segundo si el cálculo fue negativo
    }
    
    return wait_ms;
}

esp_err_t pulse_counter_init(int channel_index, int pulse_gpio_num) {
    if (channel_index < 0 || channel_index >= 3) {
        ESP_LOGE(TAG, "Invalid channel index: %d", channel_index);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (pcnt_units[channel_index] != NULL) {
        ESP_LOGW(TAG, "Channel %d already initialized", channel_index);
        return ESP_ERR_INVALID_STATE;
    }
    
    // 1. Crear unidad PCNT
    pcnt_unit_config_t unit_config = {
        .high_limit = 32767,  // Máximo valor int16_t
        .low_limit = -32768,  // Mínimo valor int16_t
    };
    
    esp_err_t ret = pcnt_new_unit(&unit_config, &pcnt_units[channel_index]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PCNT unit for channel %d: %s", 
                 channel_index, esp_err_to_name(ret));
        return ret;
    }
    
    // 2. Crear canal
    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = pulse_gpio_num,
        .level_gpio_num = -1,  // No usado
    };
    
    ret = pcnt_new_channel(pcnt_units[channel_index], &chan_config, 
                           &pcnt_channels[channel_index]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PCNT channel for channel %d: %s", 
                 channel_index, esp_err_to_name(ret));
        pcnt_del_unit(pcnt_units[channel_index]);
        pcnt_units[channel_index] = NULL;
        return ret;
    }
    
    // 3. Configurar acciones de borde
    // Si el PCNT está contando el doble (ambos flancos), puede ser que:
    // 1. HOLD no esté funcionando correctamente
    // 2. El PCNT esté interpretando los flancos de manera diferente
    // 3. La señal tenga rebotes o ruido que cause conteos adicionales
    //
    // Probando configuración invertida: contar solo en flanco de subida
    // Si esto funciona correctamente (10 cuentas en lugar de 20), entonces
    // el problema es cómo el PCNT interpreta los flancos
    ret = pcnt_channel_set_edge_action(pcnt_channels[channel_index],
                                        PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // Positivo (subida): incrementar
                                        PCNT_CHANNEL_EDGE_ACTION_HOLD);     // Negativo (bajada): mantener (no contar)
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set edge action for channel %d: %s", 
                 channel_index, esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 4. Configurar acciones de nivel (no usado, pero necesario)
    ret = pcnt_channel_set_level_action(pcnt_channels[channel_index],
                                         PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                         PCNT_CHANNEL_LEVEL_ACTION_KEEP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set level action for channel %d: %s", 
                 channel_index, esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 5. Configurar filtro de glitches
    // 100 ticks * 13ns = ~1.3μs (mismo valor que antes)
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1300,  // ~1.3μs
    };
    ret = pcnt_unit_set_glitch_filter(pcnt_units[channel_index], &filter_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set glitch filter for channel %d: %s", 
                 channel_index, esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 6. Inicializar contador
    ret = pcnt_unit_clear_count(pcnt_units[channel_index]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear count for channel %d: %s", 
                 channel_index, esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 7. Habilitar unidad PCNT (requerido antes de iniciar)
    ret = pcnt_unit_enable(pcnt_units[channel_index]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable PCNT unit for channel %d: %s", 
                 channel_index, esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 8. Iniciar contador
    ret = pcnt_unit_start(pcnt_units[channel_index]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start PCNT unit for channel %d: %s", 
                 channel_index, esp_err_to_name(ret));
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "PCNT channel %d initialized on GPIO %d", channel_index, pulse_gpio_num);
    return ESP_OK;
    
cleanup:
    if (pcnt_channels[channel_index]) {
        pcnt_del_channel(pcnt_channels[channel_index]);
        pcnt_channels[channel_index] = NULL;
    }
    if (pcnt_units[channel_index]) {
        pcnt_del_unit(pcnt_units[channel_index]);
        pcnt_units[channel_index] = NULL;
    }
    return ret;
}

esp_err_t pulse_counter_deinit(int channel_index) {
    if (channel_index < 0 || channel_index >= 3) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    
    if (pcnt_channels[channel_index]) {
        ret = pcnt_del_channel(pcnt_channels[channel_index]);
        pcnt_channels[channel_index] = NULL;
    }
    
    if (pcnt_units[channel_index]) {
        esp_err_t ret2 = pcnt_del_unit(pcnt_units[channel_index]);
        pcnt_units[channel_index] = NULL;
        if (ret == ESP_OK) ret = ret2;
    }
    
    return ret;
}

int16_t get_and_clear(int channel_index) {
    if (channel_index < 0 || channel_index >= 3 || pcnt_units[channel_index] == NULL) {
        ESP_LOGE(TAG, "Invalid channel index or unit not initialized: %d", channel_index);
        return 0;
    }
    
    int count = 0;
    esp_err_t ret = pcnt_unit_get_count(pcnt_units[channel_index], &count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get count for channel %d: %s", 
                 channel_index, esp_err_to_name(ret));
        return 0;
    }
    
    // Saturation: mantener en límites de int16_t
    if (count > 32767) {
        count = 32767;
        ESP_LOGW(TAG, "Channel %d count overflow, saturating to 32767", channel_index);
    } else if (count < -32768) {
        count = -32768;
        ESP_LOGW(TAG, "Channel %d count underflow, saturating to -32768", channel_index);
    }
    
    // Limpiar contador
    ret = pcnt_unit_clear_count(pcnt_units[channel_index]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear count for channel %d: %s", 
                 channel_index, esp_err_to_name(ret));
    }
    
    return (int16_t)count;
}

void task_pcnt(void *parameters) {
    const int32_t count_time_secs = 10;
    int32_t count[3] = { 0 };
    struct timeval tv_now, tv_start;
    struct telemetry_message message;
    
    ESP_LOGI(TAG, "Starting on Core %d", xPortGetCoreID());
    
    // Inicializar todos los canales PCNT
    esp_err_t ret;
    ret = pulse_counter_init(0, PIN_PULSE_IN_CH1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize channel 0, aborting task");
        vTaskDelete(NULL);
        return;
    }
    
    ret = pulse_counter_init(1, PIN_PULSE_IN_CH2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize channel 1, aborting task");
        pulse_counter_deinit(0);
        vTaskDelete(NULL);
        return;
    }
    
    ret = pulse_counter_init(2, PIN_PULSE_IN_CH3);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize channel 2, aborting task");
        pulse_counter_deinit(0);
        pulse_counter_deinit(1);
        vTaskDelete(NULL);
        return;
    }
    
    message.tm_message_type = TM_PULSE_COUNT;
    
    // Reconfigurar interrupciones GPIO después de inicializar PCNT
    // (PCNT puede haber sobrescrito la configuración GPIO)
    // Solo si la detección por GPIO está habilitada
#ifdef CONFIG_ENABLE_GPIO_PULSE_DETECTION
    reconfigure_GPIO_interrupts();
#endif

    // Esperar hasta el primer segundo alineado y descartar el primer conteo
    int next_aligned = 0;
    int64_t wait_ms = calculate_wait_time_to_aligned_second(&next_aligned);
    
    ESP_LOGI(TAG, "First count discarded, waiting %lld ms (%.3f s) until next aligned second (%d)", 
                wait_ms, (double)wait_ms / 1000.0, next_aligned);
    vTaskDelay(pdMS_TO_TICKS((TickType_t)wait_ms));
    // Discard the first count
    get_and_clear(0);
    get_and_clear(1);
    get_and_clear(2);
    
    while (true) {

        // Obtener timestamp de inicio del intervalo (ahora estamos en un segundo alineado)
        gettimeofday(&tv_start, NULL);
        message.payload.tm_pcnt.start_timestamp = 
            (int64_t)tv_start.tv_sec * 1000000LL + (int64_t)tv_start.tv_usec;

        // Esperar hasta el próximo segundo alineado (10, 20, 30, 40, 50, 0) antes de empezar a contar
        int next_aligned = 0;
        int64_t wait_ms = calculate_wait_time_to_aligned_second(&next_aligned);
        
        ESP_LOGI(TAG, "Waiting %lld ms (%.3f s) until next aligned second (%d)", 
                 wait_ms, (double)wait_ms / 1000.0, next_aligned);
        
        vTaskDelay(pdMS_TO_TICKS((TickType_t)wait_ms));
        
        // Obtener timestamp de fin del intervalo
        gettimeofday(&tv_now, NULL);
        message.timestamp = (int64_t)tv_now.tv_sec * 1000000LL + (int64_t)tv_now.tv_usec;
        
        // Leer y limpiar contadores
        count[0] = get_and_clear(0);
        count[1] = get_and_clear(1);
        count[2] = get_and_clear(2);
        
        // Formatear timestamp en formato ISO 8601
        struct tm timeinfo;
        char timestamp_str[32];
        localtime_r(&tv_now.tv_sec, &timeinfo);
        strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%dT%H:%M:%S", &timeinfo);
        // Añadir microsegundos y Z al final
        int len = strlen(timestamp_str);
        snprintf(timestamp_str + len, sizeof(timestamp_str) - len, ".%06ldZ", (long)tv_now.tv_usec);
        
        // Mostrar resumen formateado similar a SPL06
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Pulse Count Reading:");
        ESP_LOGI(TAG, "  Channel 1:    %d pulses", (int)count[0]);
        ESP_LOGI(TAG, "  Channel 2:    %d pulses", (int)count[1]);
        ESP_LOGI(TAG, "  Channel 3:    %d pulses", (int)count[2]);
        ESP_LOGI(TAG, "  Interval:     %ld seconds", (long)count_time_secs);
        ESP_LOGI(TAG, "  Timestamp:    %s", timestamp_str);
        ESP_LOGI(TAG, "========================================");
        
        // Preparar mensaje de telemetría
        message.payload.tm_pcnt.integration_time_sec = count_time_secs;
        message.payload.tm_pcnt.channel[0] = (uint32_t)count[0];
        message.payload.tm_pcnt.channel[1] = (uint32_t)count[1];
        message.payload.tm_pcnt.channel[2] = (uint32_t)count[2];
        
        // Enviar mensaje a la cola
        if (xQueueSend(telemetry_queue, &message, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send message to telemetry queue (queue full or timeout)");
        } else {
            ESP_LOGI(TAG, "Pulse count message sent to telemetry queue (ch1=%d, ch2=%d, ch3=%d)", 
                     (int)count[0], (int)count[1], (int)count[2]);
        }
        
        // El bucle volverá al inicio y esperará hasta el siguiente segundo alineado
    }
}
