#include "botao.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "mainGlobals.h"

static const char *TAG = "BOTAO_ISR";

// Handle da tarefa para podermos notificá-la de dentro da ISR
static TaskHandle_t s_task_botao_handle = NULL;

// --- ISR (Rotina de Interrupção) ---
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    // Apenas avisa a tarefa que algo aconteceu
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task_botao_handle, &xHigherPriorityTaskWoken);
    
    // Se a tarefa do botão for mais prioritária que a atual, força troca de contexto agora
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// --- Tarefa de Tratamento ---
static void task_botao_event(void *pvParameters) {
    const int64_t DEBOUNCE_TIME_US = 200000; // 200ms de debounce
    static int64_t last_interrupt_time = 0;

    while (1) {
        // 1. Dorme e espera o sinal da ISR. 
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 2. Acordou, verifica o Debounce pelo tempo
        int64_t interrupt_time = esp_timer_get_time(); // Tempo em microssegundos

        if (interrupt_time - last_interrupt_time > DEBOUNCE_TIME_US) {
            
            if (gpio_get_level(BOTAO_PIN) == 0) {
                
                ESP_LOGI(TAG, "Clique valido detectado via ISR!");

                // 3. Lógica do Setpoint (Segura com Mutex)
                if (xSemaphoreTake(mutex_pr, pdMS_TO_TICKS(100)) == pdTRUE) {
                    // Alterna entre 0 e 80
                    if (pr[1] == 0.0f) {
                        pr[1] = -80.0f;
                    } else {
                        pr[1] = 0.0f;
                    }
                    ESP_LOGI(TAG, "Novo Roll definido para: %.2f", pr[1]);
                    xSemaphoreGive(mutex_pr);
                } else {
                    ESP_LOGW(TAG, "Nao conseguiu pegar o Mutex a tempo.");
                }

                last_interrupt_time = interrupt_time;
            }
        }
    }
}

void botao_init_isr_task(void) {
    // 1. Configura o GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOTAO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE // Interrupção apenas na descida (apertar)
    };
    gpio_config(&io_conf);

    // 2. Cria a tarefa
    xTaskCreatePinnedToCore(task_botao_event, "task_btn_evt", 2048, NULL, 5, &s_task_botao_handle, 0);

    // 3. Adiciona o serviço de ISR e adiciona o handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BOTAO_PIN, gpio_isr_handler, NULL);

    ESP_LOGI(TAG, "Botao configurado com ISR.");
}