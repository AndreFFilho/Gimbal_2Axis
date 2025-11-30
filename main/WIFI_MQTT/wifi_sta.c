#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi_sta.h"
#include <string.h>

// --- CONFIGURAÇÃO DAS REDES (Prioridade: Topo -> Base) ---
typedef struct {
    char ssid[32];
    char password[64];
} wifi_creds_t;

// ADICIONE SUAS REDES AQUI
static const wifi_creds_t wifi_networks[] = {
    { .ssid = "SEU_WIFI_1_AQUI",     .password = "SUA_SENHA_1_AQUI" },   // Prioridade 1
    { .ssid = "SEU_WIFI_2_AQUI",     .password = "SUA_SENHA_2_AQUI" },   // Prioridade 2
    { .ssid = "SEU_WIFI_3_AQUI",     .password = "SUA_SENHA_3_AQUI" }    // Prioridade 3
};

#define NUM_NETWORKS (sizeof(wifi_networks) / sizeof(wifi_networks[0]))
#define WIFI_MAX_RETRY 5

static const char *TAG = "WIFI";
static EventGroupHandle_t s_wifi_event_group;

// Variáveis de controle de estado
static int s_retry_num = 0;
static int s_current_network_index = 0; // Começa na rede 0

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Função auxiliar para configurar e conectar à rede atual baseada no índice
static void connect_to_current_network() {
    wifi_config_t wifi_config = { 0 };
    
    // Copia SSID e Senha da lista atual
    strlcpy((char *)wifi_config.sta.ssid, wifi_networks[s_current_network_index].ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, wifi_networks[s_current_network_index].password, sizeof(wifi_config.sta.password));
    
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "Configurando para conectar em: %s (Tentativa rede %d de %d)", 
             wifi_config.sta.ssid, s_current_network_index + 1, NUM_NETWORKS);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();
}

// --- Manipulador de Eventos WiFi ---
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Ao iniciar, tenta a primeira rede da lista
        connect_to_current_network();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            // Tenta reconectar na MESMA rede algumas vezes
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Tentando reconectar em %s (%d/%d)", 
                     wifi_networks[s_current_network_index].ssid, s_retry_num, WIFI_MAX_RETRY);
        } else {
            // Falhou muitas vezes na rede atual, vamos tentar a PRÓXIMA
            ESP_LOGW(TAG, "Falha ao conectar em %s. Tentando próxima rede...", wifi_networks[s_current_network_index].ssid);
            
            s_retry_num = 0;
            s_current_network_index++;

            if (s_current_network_index < NUM_NETWORKS) {
                // Ainda tem redes na lista para tentar
                connect_to_current_network();
            } else {
                // Acabaram as redes da lista, falha total
                ESP_LOGE(TAG, "Todas as redes falharam.");
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conectado! IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// --- Inicializa o WiFi e conecta às redes configuradas ---
void wifi_init_sta(void) {
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    
    // Netif / Eventos
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // O esp_wifi_start dispara o evento WIFI_EVENT_STA_START, 
    // que chama nossa função connect_to_current_network()
    ESP_ERROR_CHECK(esp_wifi_start());

    // Espera conectar ou falhar
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Sucesso: Conectado à rede %s", wifi_networks[s_current_network_index].ssid);
    } else {
        ESP_LOGE(TAG, "Falha Geral: Não foi possível conectar em nenhuma rede.");
    }
}