// --- Includes Padrão e de Biblioteca ---
#include <stdio.h>
#include <stdlib.h>
#include "log_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// --- Includes do Projeto ---
#include "adc_bateria.h"
#include "mqtt_esp32.h"

// --- Tag de Log ---
static const char *TAG = "BAT_MONITOR";

// --- Configurações de Hardware e Físicas ---
// Divisor de tensão: Vout = Vin * R2 / (R1 + R2)
#define RESISTOR_R1_OHMS        100000.0
#define RESISTOR_R2_OHMS        51000.0
// Fator de correção para voltar à tensão original: (R1+R2)/R2
#define VOLTAGE_DIVIDER_FACTOR  ((RESISTOR_R1_OHMS + RESISTOR_R2_OHMS) / RESISTOR_R2_OHMS)

// --- Configurações do ADC ---
#define ADC_UNIT                ADC_UNIT_1
#define ADC_CHANNEL             ADC_CHANNEL_6   // GPIO34 no ESP32
#define ADC_ATTEN               ADC_ATTEN_DB_12 // Permite leitura até ~3.1V (ESP-IDF v5.x)
#define ADC_SAMPLES_COUNT       64              // Média móvel simples

// --- Configurações de Alerta ---
#define PIN_LED_STATUS          32              // GPIO do LED de status da bateria
#define BAT_LOW_THRESHOLD_V     6.4             // Tensão mínima para alerta
#define UPDATE_INTERVAL_MS      5000            // Intervalo de leitura

// --- Variáveis Estáticas (Escopo do Arquivo) ---
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;
static bool calibration_valid = false;
static bool led_state = true; 

// --- Inicializa a calibração do ADC ---
static bool init_adc_calibration(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle){
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    // 1. Tentar Curve Fitting (Preferencial)
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

    // 2. Tentar Line Fitting (Fallback)
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

    *out_handle = handle;
    
    if (calibrated) {
        ESP_LOGI(TAG, "Calibração ADC ativada.");
    } else {
        ESP_LOGW(TAG, "Calibração não suportada ou eFuse não queimado. Usando valores raw.");
    }

    return calibrated;
}

// --- Obtém a tensão do pino ADC em mV ---
static int obter_tensao_pino_mv(void){
    int adc_raw_sum = 0;
    int adc_raw_avg = 0;
    int voltage_mv = 0;

    // 1. Coleta de amostras (Oversampling)
    for (int i = 0; i < ADC_SAMPLES_COUNT; i++) {
        int raw;
        if (adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw) == ESP_OK) {
            adc_raw_sum += raw;
        }
    }
    adc_raw_avg = adc_raw_sum / ADC_SAMPLES_COUNT;

    // 2. Conversão para mV
    if (calibration_valid) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, adc_raw_avg, &voltage_mv));
    } else {
        // Fallback manual aproximado se não houver calibração
        voltage_mv = (adc_raw_avg * 2500) / 4095;
    }

    return voltage_mv;
}

// --- Configura o ADC e GPIO (LED) ---
void setup_adc(void){
    // 1. Configura a Unidade ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    // 2. Configura o Canal ADC
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config));

    // 3. Configura a Calibração
    calibration_valid = init_adc_calibration(ADC_UNIT, ADC_CHANNEL, ADC_ATTEN, &cali_handle);

    // 4. Configura o LED
    LOGI(TAG, "Configurando GPIO %d (LED)...", PIN_LED_STATUS);
    gpio_reset_pin(PIN_LED_STATUS);
    gpio_set_direction(PIN_LED_STATUS, GPIO_MODE_OUTPUT);
    
    // Estado inicial: LED Aceso
    led_state = true;
    gpio_set_level(PIN_LED_STATUS, 1);
}

// --- Task de Leitura e Monitoramento da Bateria ---
void task_leitura_bateria(void *pvParameters){
    LOGI(TAG, "Iniciando monitoramento de bateria...");

    while (1) 
    {
        // 1. Leitura e Conversão
        int pino_mv = obter_tensao_pino_mv();
        
        // 2. Cálculo da tensão real
        double bat_voltage_v = ((double)pino_mv * VOLTAGE_DIVIDER_FACTOR) / 1000.0;

        // 3. Publicação MQTT
        mqtt_publish_battery_voltage(bat_voltage_v);

        // 4. Lógica do LED de Status
        if (bat_voltage_v > 0.5 && bat_voltage_v <= BAT_LOW_THRESHOLD_V) { // > 0.5 para ignorar leituras espúrias de 0V
            // Bateria Baixa: Pisca (Inverte estado atual)
            led_state = !led_state;
            gpio_set_level(PIN_LED_STATUS, led_state);
            LOGW(TAG, "Bateria Baixa: %.2f V", bat_voltage_v);
        } else {
            // Bateria OK: Mantém Aceso
            if (!led_state) {
                led_state = true;
                gpio_set_level(PIN_LED_STATUS, 1);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(UPDATE_INTERVAL_MS));
    }
}