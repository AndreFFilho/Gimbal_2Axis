idf_component_register(SRCS "main.c" "MPU6050/SensorMPU6050.cpp" "PID/ControladorPID.cpp" "WIFI_MQTT/mqtt_esp32.c" "WIFI_MQTT/wifi_sta.c" "BATERIA/adc_bateria.c" "BUFFER/BufferTelemetria.c" "BOTAO/botao.c" 
                    INCLUDE_DIRS "." "MPU6050" "PID" "WIFI_MQTT" "BATERIA" "BUFFER" "BOTAO" "LOGGER"
                    REQUIRES esp_wifi esp_event esp_netif esp_adc nvs_flash mqtt json
                    PRIV_REQUIRES MPU6050)