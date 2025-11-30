#ifndef LOG_MQTT_H
#define LOG_MQTT_H

#include "esp_log.h"
#include "mqtt_esp32.h"

#define LOGI(tag, fmt, ...) \
    do { \
        ESP_LOGI(tag, fmt, ##__VA_ARGS__); \
        mqtt_publish_logf(tag, "INFO", fmt, ##__VA_ARGS__); \
    } while (0)

#define LOGW(tag, fmt, ...) \
    do { \
        ESP_LOGW(tag, fmt, ##__VA_ARGS__); \
        mqtt_publish_logf(tag, "WARN", fmt, ##__VA_ARGS__); \
    } while (0)

#define LOGE(tag, fmt, ...) \
    do { \
        ESP_LOGE(tag, fmt, ##__VA_ARGS__); \
        mqtt_publish_logf(tag, "ERROR", fmt, ##__VA_ARGS__); \
    } while (0)

#endif
