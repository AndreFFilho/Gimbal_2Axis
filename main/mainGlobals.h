// main/mainGlobals.h

#ifndef MAINGLOBALS_H
#define MAINGLOBALS_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// --- Declarações Globais Compartilhadas ---

// [pitch, roll]   Ângulos alvo de Pitch e Roll em graus
extern float pr[2];

// [pitch, roll]   Ângulos medidos de Pitch e Roll em graus
extern float pr_medido[2];

// Mutex para proteger o acesso à variável pr
extern SemaphoreHandle_t mutex_pr;

// Mutex para proteger o acesso à variável pr_medido
extern SemaphoreHandle_t mutex_sensor_data;

// Semáforo para indicar que o MPU está pronto
extern SemaphoreHandle_t g_mpu_pronta;

// Fila para enviar telemetria de [pitch, roll] para a tarefa MQTT
extern QueueHandle_t queue_telemetry;

#endif // MAINGLOBALS_H