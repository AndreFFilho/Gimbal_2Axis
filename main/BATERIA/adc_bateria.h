#ifndef ADC_BATERIA_H
#define ADC_BATERIA_H

#include <stdint.h>

/**
 * @brief Inicializa o ADC e o GPIO do LED de status.
 * Deve ser chamada antes de iniciar a task ou ler valores.
 */
void setup_adc(void);

/**
 * @brief Task principal para monitoramento da bateria.
 * Realiza leitura, converte para tensão real e gerencia o LED de alerta.
 */
void task_leitura_bateria(void *pvParameters);

#endif // ADC_BATERIA_H