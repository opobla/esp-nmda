#ifndef __DATASTRUCTURES__H_
#define __DATASTRUCTURES__H_

#define TM_METEO 1
#define TM_PULSE_DETECTION 2
#define TM_PULSE_COUNT 3
#define TM_TIME_SYNCHRONIZER 4

#ifdef CONFIG_ENABLE_SPL06
#define TM_SPL06 6
#endif

#ifdef CONFIG_ENABLE_RMT_PULSE_DETECTION
#define TM_RMT_PULSE_EVENT 7
#define TM_RMT_COINCIDENCE 8
#define TM_RMT_MULTIPLICITY 9

// Structure for a single pulse (duration and separation)
typedef struct {
    uint32_t duration_us;   // Duración del pulso (microsegundos)
    int64_t separation_us;   // Separación con pulso anterior (microsegundos, -1 si es el primero)
} rmt_pulse_t;
#endif

struct telemetry_message {
    uint8_t tm_message_type;
    int64_t timestamp;
    union {
        struct  {
            uint32_t atm_pressure_hpas;
            float temperature_celsius;
        } tm_meteo;
        struct  {
            uint8_t integration_time_sec;
            uint32_t channel[3];
            int64_t start_timestamp;  // Timestamp de inicio del intervalo (microsegundos Unix)
        } tm_pcnt;
        struct {
            uint32_t channel[3];
        } tm_detect;
        struct {
            uint32_t cpu_count;
        } tm_sync;
#ifdef CONFIG_ENABLE_SPL06
        struct {
            float pressure_pa;          // Presión en Pascales
            float pressure_hpa;         // Presión en hectopascales (para compatibilidad)
            float temperature_celsius;  // Temperatura en grados Celsius
            float qnh_hpa;              // QNH (presión reducida al nivel del mar) en hPa (fórmula AEMET)
        } tm_spl06;
#endif
#ifdef CONFIG_ENABLE_RMT_PULSE_DETECTION
        struct {
            uint8_t channel;            // Canal (1, 2, o 3 - convenio ch1, ch2, ch3)
            uint8_t symbols;            // Número de símbolos/pulsos en este grupo
            int64_t start_timestamp;    // Timestamp de inicio del primer pulso (microsegundos Unix)
            // Array dinámico de pulsos: cada pulso tiene duración y separación
            // Se asigna memoria dinámicamente según el número real de pulsos (símbolos elementos)
            // La memoria debe ser liberada por el consumidor del mensaje (mss_sender)
            rmt_pulse_t *pulses;  // Puntero a array dinámico (símbolos elementos)
        } tm_rmt_pulse_event;
        struct {
            uint8_t type;               // Tipo: COINC_2_CH01, COINC_2_CH12, COINC_2_CH02, COINC_3
            uint8_t num_channels;       // Número de canales en la coincidencia (2 o 3)
            uint8_t channels[3];        // Canales involucrados (bitmask o índices)
            uint32_t channel_duration[3]; // Duración de pulso por canal (microsegundos)
            int64_t channel_separation[3]; // Separación con pulso anterior por canal (microsegundos)
        } tm_rmt_coincidence;
        struct {
            uint8_t channel;            // Canal donde se detectó la multiplicidad
            uint8_t count;              // Número de pulsos en el grupo
            uint32_t max_separation_us; // Máxima separación entre pulsos (microsegundos)
            uint32_t total_duration_us; // Duración total del grupo (microsegundos)
        } tm_rmt_multiplicity;
#endif
  } payload;
};

#endif
