idf_component_register(SRCS "sdcard.c" "settings.c" "main.c" "wifi.c" "sntp.c" "mqtt.c"
 "ota.c" "mss_sender.c" "pulse_monitor.c" "pulse_detection.c" "sdcard.c" 

                    INCLUDE_DIRS "." "include" 
                    EMBED_TXTFILES ../certificate/github.cer
                    EMBED_TXTFILES ../certificate/hash)
