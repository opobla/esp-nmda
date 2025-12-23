idf_component_register(SRCS "sdcard.c" "settings.c" "main.c" "wifi.c" "sntp.c" "mqtt.c"
 "ota.c" "mss_sender.c" "pulse_monitor.c" "pulse_detection.c" "i2c_bus.c" "spl06.c" "spl06_monitor_task.c" "hv_adc.c" "hv_adc_monitor_task.c" "user_led.c"

                    INCLUDE_DIRS "." "include" 
                    EMBED_TXTFILES ../certificate/github.cer
                    EMBED_TXTFILES ../certificate/hash)
