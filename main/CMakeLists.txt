idf_component_register(SRCS "meteo_bmp280.c" "settings.c" "main.c" "wifi.c" "sntp.c" "mqtt.c" "meteo.c" "ota.c" "mss_sender.c" "pulse_monitor.c" "pulse_detection.c" "BMP2-Sensor-API/bmp2.c"
                    INCLUDE_DIRS "." "include" "BMP2-Sensor-API"
                    EMBED_TXTFILES ../certificate/github.cer
                    EMBED_TXTFILES ../certificate/hash)
