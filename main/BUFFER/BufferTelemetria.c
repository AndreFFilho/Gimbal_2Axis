#include "BufferTelemetria.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "BUFFER_TELEMETRIA";

// Buffer linear: capacidade * 2 (pitch, roll)
static float *buffer = NULL;

static size_t capacidade_buffer = 0;
static size_t indice_escrita = 0;
static size_t indice_leitura  = 0;

static SemaphoreHandle_t mutex_buffer = NULL;
static SemaphoreHandle_t sem_preenchido = NULL; // Quantos dados existem
static SemaphoreHandle_t sem_livre = NULL;      // Quantos espaços existem

// Inicia o buffer de telemetria com a capacidade especificada (número de pares pitch/roll)
bool buffer_telemetria_iniciar(size_t capacidade){
    if (capacidade == 0) return false;
    if (buffer != NULL) return true;            // Já iniciado

    buffer = (float*)malloc(sizeof(float) * capacidade * 2);
    if (!buffer) {
        ESP_LOGI(TAG, "Falha no malloc do buffer");
        return false;
    }

    memset(buffer, 0, sizeof(float) * capacidade * 2);

    capacidade_buffer = capacidade;
    indice_escrita = 0;
    indice_leitura = 0;

    mutex_buffer = xSemaphoreCreateMutex();
    sem_preenchido = xSemaphoreCreateCounting(capacidade, 0);
    sem_livre     = xSemaphoreCreateCounting(capacidade, capacidade);

    if (!mutex_buffer || !sem_preenchido || !sem_livre) {
        ESP_LOGI(TAG, "Falha ao criar semáforos");
        buffer_telemetria_finalizar();
        return false;
    }

    ESP_LOGI(TAG, "Buffer de telemetria iniciado. Capacidade = %d", (int)capacidade);
    return true;
}

// Grava um par (pitch, roll) no buffer. Retorna false se o buffer estiver cheio após o tempo de espera especificado
bool buffer_telemetria_gravar(const float dado[2], TickType_t espera_ticks){
    if (!buffer) return false;

    // Espera até existir espaço livre
    if (xSemaphoreTake(sem_livre, espera_ticks) != pdTRUE) {
        return false; // Buffer cheio
    }

    if (xSemaphoreTake(mutex_buffer, portMAX_DELAY) != pdTRUE) {
        xSemaphoreGive(sem_livre);
        return false; // Falha ao pegar mutex
    }

    // Grava no índice atual
    size_t pos = indice_escrita * 2;
    buffer[pos]     = dado[0]; // pitch
    buffer[pos + 1] = dado[1]; // roll

    // Atualiza índice de escrita
    indice_escrita = (indice_escrita + 1) % capacidade_buffer;

    // Libera mutex e sinaliza dado preenchido
    xSemaphoreGive(mutex_buffer);
    xSemaphoreGive(sem_preenchido);

    // Sucesso
    return true;
}

// Lê um par (pitch, roll) do buffer. Retorna false se o buffer estiver vazio após o tempo de espera especificado
bool buffer_telemetria_ler(float saida[2], TickType_t espera_ticks) {
    if (!buffer) return false;

    // Espera até existir dado salvo
    if (xSemaphoreTake(sem_preenchido, espera_ticks) != pdTRUE) {
        return false; // Buffer vazio
    }

    // Pega mutex do buffer
    if (xSemaphoreTake(mutex_buffer, portMAX_DELAY) != pdTRUE) {
        xSemaphoreGive(sem_preenchido);
        return false; // Falha ao pegar mutex
    }

    // Lê do índice atual
    size_t pos = indice_leitura * 2;
    saida[0] = buffer[pos];
    saida[1] = buffer[pos + 1];

    // Atualiza índice de leitura
    indice_leitura = (indice_leitura + 1) % capacidade_buffer;

    // Libera mutex e sinaliza espaço livre
    xSemaphoreGive(mutex_buffer);
    xSemaphoreGive(sem_livre);

    // Sucesso
    return true;
}

// Finaliza o buffer de telemetria, liberando recursos
void buffer_telemetria_finalizar(void){
    if (buffer) {
        free(buffer);
        buffer = NULL;
    }

    capacidade_buffer = 0;
    indice_escrita = 0;
    indice_leitura = 0;

    if (mutex_buffer) { vSemaphoreDelete(mutex_buffer); mutex_buffer = NULL; }
    if (sem_preenchido) { vSemaphoreDelete(sem_preenchido); sem_preenchido = NULL; }
    if (sem_livre) { vSemaphoreDelete(sem_livre); sem_livre = NULL; }

    ESP_LOGI(TAG, "Buffer de telemetria finalizado");
}
