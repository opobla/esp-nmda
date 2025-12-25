#ifndef __COMMON_H__
#define __COMMON_H__

#ifndef CONFIG_LOG_MAXIMUM_LEVEL
#define CONFIG_LOG_MAXIMUM_LEVEL 4
#endif

#include "esp32-libs.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

//OTA
void task_ota(void *parameters);

//LOG
int mqtt_logging(const char *fmt, va_list l);

//MSS SENDER
void mss_sender(void *parameters);

//PULSE
void task_pcnt(void *parameters);

//SPL06
#ifdef CONFIG_ENABLE_SPL06
void spl06_monitor_task(void *parameters);
#endif

#ifdef CONFIG_ENABLE_HV_SUPPORT
void hv_adc_monitor_task(void *parameters);
#endif

//SET UP
void init_GPIO(void);

//QUEUE
extern QueueHandle_t telemetry_queue;

//SEMAPHORE
extern SemaphoreHandle_t wifi_semaphore;
extern SemaphoreHandle_t sntp_semaphore;
extern SemaphoreHandle_t mqtt_semaphore;
extern SemaphoreHandle_t dtct_semaphore;

#endif
