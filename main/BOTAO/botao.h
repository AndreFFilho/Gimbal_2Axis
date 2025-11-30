#ifndef BOTAO_H
#define BOTAO_H

#include "driver/gpio.h"

// Pino do botão
#define BOTAO_PIN GPIO_NUM_33

// Inicializa o botão com interrupção e cria a task de tratamento
void botao_init_isr_task(void);

#endif