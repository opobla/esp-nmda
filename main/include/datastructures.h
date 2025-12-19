#ifndef __DATASTRUCTURES__H_
#define __DATASTRUCTURES__H_

#define TM_METEO 1
#define TM_PULSE_DETECTION 2
#define TM_PULSE_COUNT 3
#define TM_TIME_SYNCHRONIZER 4

#ifdef CONFIG_ENABLE_SPL06
#define TM_SPL06 6
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
        } tm_spl06;
#endif
  } payload;
};

#endif
