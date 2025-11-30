// main/WIFI_MQTT/mqtt_esp32.h

#ifndef MQTT_ESP32_H
#define MQTT_ESP32_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tarefa de inicialização do cliente MQTT
 */
void mqtt_start(void);

/**
 * @brief Tarefa de publicação de mensagens MQTT
 */
void mqtt_publish_telemetry(float pitch, float roll);

/**
 * @brief Publica a tensão da bateria via MQTT
 */
void mqtt_publish_battery_voltage(double voltage);

/**
 * @brief Publica mensagem de log via MQTT (JSON)
 */
void mqtt_publish_logf(const char *tag, const char *level, const char *fmt, ...);


#ifdef __cplusplus
}
#endif

#endif // MQTT_CLIENT_H