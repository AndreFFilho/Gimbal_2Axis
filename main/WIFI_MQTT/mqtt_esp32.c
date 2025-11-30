#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt_esp32.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mainGlobals.h"

// ---------------------------
// Tópicos (GUI <-> ESP32)
// ---------------------------
#define TOPIC_CMD "gimbal/cmd"   // GUI -> ESP32 (comando JSON)
#define TOPIC_TEL "gimbal/tel"   // ESP32 -> GUI (telemetria JSON)
#define TOPIC_LOG "gimbal/log"   // Logs do ESP32 -> PC


// ---------------------------
// Broker (troque para o IP/URI do seu Mosquitto)
#define MQTT_URI  "SEU_BROKER_AQUI"  // ex: "mqtts://test.mosquitto.org:8883"
// ---------------------------

static const char *TAG = "MQTT_GIMBAL";
static esp_mqtt_client_handle_t s_client = NULL;

// --- Publica telemetria ---
void mqtt_publish_telemetry(float pitch, float roll) {
    if (!s_client) return;

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "pitch", pitch);
    cJSON_AddNumberToObject(root, "roll",  roll);

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        esp_mqtt_client_publish(s_client, TOPIC_TEL, out, 0, 0, 0);
        free(out);
    }
    cJSON_Delete(root);
}

// --- Publica tensão da bateria ---
void mqtt_publish_battery_voltage(double voltage) {
    if (!s_client) return;

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "vbat", voltage);

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        esp_mqtt_client_publish(s_client, TOPIC_TEL, out, 0, 0, 0);
        free(out);
    }
    cJSON_Delete(root);
}

// --- Publica logs de erro ---
void mqtt_publish_logf(const char *tag, const char *level, const char *fmt, ...) {
    if (!s_client) {
        return;  // Ainda não conectado ao broker
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    // tag: ex. "MAIN", "MPU6050"
    cJSON_AddStringToObject(root, "tag",   tag   ? tag   : "");
    cJSON_AddStringToObject(root, "level", level ? level : "INFO");

    char msg_buf[160];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, ap);
    va_end(ap);

    cJSON_AddStringToObject(root, "msg", msg_buf);

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        esp_mqtt_client_publish(s_client, TOPIC_LOG, out, 0, 0, 0);
        free(out);
    }

    cJSON_Delete(root);
}

// --- Aplica comando JSON recebido: atualiza pr[] ---
static void apply_cmd_json(const char *payload, int len) {
    if (!payload || len <= 0) return;

    // Garante string \0-terminada para o cJSON
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) return;
    memcpy(buf, payload, (size_t)len);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGW(TAG, "JSON inválido");
        return;
    }

    const cJSON *jp = cJSON_GetObjectItemCaseSensitive(root, "pitch");
    const cJSON *jr = cJSON_GetObjectItemCaseSensitive(root, "roll");

    if (cJSON_IsNumber(jp) && cJSON_IsNumber(jr)) {
        xSemaphoreTake(mutex_pr, portMAX_DELAY);
        pr[0] = (float)jp->valuedouble;  // setpoint de pitch
        pr[1] = (float)jr->valuedouble;  // setpoint de roll
        xSemaphoreGive(mutex_pr);
    } else {
        ESP_LOGW(TAG, "JSON sem campos numéricos 'pitch'/'roll'");
    }

    cJSON_Delete(root);
}

// --- Handler de eventos do cliente MQTT ---
static void _mqtt_event_handler(void *arg, esp_event_base_t base, int32_t eid, void *edata) {
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t) edata;

    switch (eid) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado ao broker: %s", MQTT_URI);
        esp_mqtt_client_subscribe(s_client, TOPIC_CMD, 0);
        esp_mqtt_client_publish(s_client, "gimbal/status", "online", 0, 0, 1);
        break;

    case MQTT_EVENT_DATA:
        // Chegou mensagem. Se for no TOPIC_CMD, aplica.
        if (e->topic && e->data && e->topic_len > 0 && e->data_len > 0) {
            // Confere se o tópico é exatamente TOPIC_CMD
            if (strncmp(e->topic, TOPIC_CMD, e->topic_len) == 0
                && strlen(TOPIC_CMD) == (size_t)e->topic_len) {
                apply_cmd_json(e->data, e->data_len);
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT_EVENT_ERROR");
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    default:
        // outros eventos (PUBACK, SUBACK, etc.)
        break;
    }
}

// --- Inicia cliente MQTT ---
void mqtt_start(void) {
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_URI,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach, // Habilita TLS com certificados padrão        
        .credentials = {
            .username = "SEU_USUARIO_AQUI",                // Troque para seu usuário MQTT
            .authentication.password = "SEU_SENHA_AQUI",   // Troque para sua senha MQTT
        },
    };

    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, _mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
}
