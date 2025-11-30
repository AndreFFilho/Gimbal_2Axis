#ifndef BUFFER_TELEMETRIA_H
#define BUFFER_TELEMETRIA_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa o buffer circular de telemetria.
bool buffer_telemetria_iniciar(size_t capacidade);

// Insere no buffer um par [pitch, roll].
bool buffer_telemetria_gravar(const float dado[2], TickType_t espera_ticks);

// Retira um par [pitch, roll] do buffer.
bool buffer_telemetria_ler(float saida[2], TickType_t espera_ticks);

// Finaliza e libera memória.
void buffer_telemetria_finalizar(void);

#ifdef __cplusplus
}
#endif

#endif // BUFFER_TELEMETRIA_H
