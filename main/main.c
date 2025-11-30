#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_mqtt.h"
#include "esp_err.h"
#include "mainGlobals.h"
#include "SensorMPU6050.h"
#include "ControladorPID.h"
#include "mqtt_esp32.h"
#include "wifi_sta.h"
#include "adc_bateria.h"
#include "botao.h"
#include "BufferTelemetria.h"

// --- Declarações Globais Compartilhadas ---
float pr[2] = {0.0f, 0.0f};             // [pitch, roll]   Ângulos alvo de Pitch e Roll em graus
float pr_medido[2] = {0.0f, 0.0f};      // [pitch, roll]   Ângulos medidos de Pitch e Roll em graus
SemaphoreHandle_t mutex_pr;             // Mutex para proteger o acesso à variável pr
SemaphoreHandle_t mutex_sensor_data;    // Mutex para proteger o acesso à variável pr_medido
SemaphoreHandle_t g_mpu_pronta;         // Semáforo para indicar que o MPU está pronto

void task_mqtt_publish(void *pvParameters) {
    float dados[2];
    while (1) {
        // Aguarda até haver dados no buffer circular
        if (buffer_telemetria_ler(dados, portMAX_DELAY)) {
            mqtt_publish_telemetry(dados[0], dados[1]);
        }
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    LOGI("MAIN", "Iniciando aplicação...");

    // Inicializa mutex e queue
    mutex_pr = xSemaphoreCreateMutex();
    mutex_sensor_data = xSemaphoreCreateMutex();
    g_mpu_pronta = xSemaphoreCreateBinary();

    const size_t CAPACIDADE_BUFFER_TELEMETRIA = 200;
    if (!buffer_telemetria_iniciar(CAPACIDADE_BUFFER_TELEMETRIA)) {
        LOGE("MAIN", "Falha ao iniciar buffer de telemetria");
    }

    LOGI("MAIN", "Globais (Mutex/Filas) criadas.");

    wifi_init_sta();
    mqtt_start();
    setup_adc();
    botao_init_isr_task();

    xTaskCreatePinnedToCore(task_initI2C, "task_initI2C", 2048, NULL, 10, NULL, 1);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    xTaskCreatePinnedToCore(task_mpu, "task_mpu", 8192, NULL, 10, NULL, 1);
    LOGI("MAIN", "Task MPU criada.");
    xTaskCreatePinnedToCore(task_pid, "task_pid", 4096, NULL, 9, NULL, 1);
    LOGI("MAIN", "Task PID criada.");
    xTaskCreatePinnedToCore(task_mqtt_publish, "task_mqtt_publish", 4096, NULL, 3, NULL, 0);
    LOGI("MAIN", "Task MQTT Publish criada.");
    xTaskCreatePinnedToCore(task_leitura_bateria, "task_leitura_bateria", 2048, NULL, 2, NULL, 0);
    LOGI("MAIN", "Task Leitura Bateria criada.");
}
