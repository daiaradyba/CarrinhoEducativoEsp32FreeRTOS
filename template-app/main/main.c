#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "driver/gpio.h"

#define WIFI_SSID      "Dyba_Bernardelli_2G"
#define WIFI_PASS      "123581321"

static const char *TAG = "wifi_http_example";



esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER");
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                printf("%.*s\n", evt->data_len, (char*)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT: // Adicionado tratamento para HTTP_EVENT_REDIRECT
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
        default:
            ESP_LOGD(TAG, "Unhandled event (%d)", evt->event_id);
            break;
    }
    return ESP_OK;
}
// Função para realizar a requisição HTTP GET
void http_get_task(void *pvParameters) {
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/get",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_perform(client);

    // Limpeza
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

// Handler para eventos do Wi-Fi
void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Tentando reconectar ao AP...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Conectado com sucesso!");
        // Chama a tarefa HTTP após a conexão ser estabelecida
        xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 5, NULL);
    }
}

// Configuração e inicialização do Wi-Fi
void wifi_init() {
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &event_handler,
                                        NULL,
                                        &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT,
                                        IP_EVENT_STA_GOT_IP,
                                        &event_handler,
                                        NULL,
                                        &instance_got_ip);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
}

void vtask_blink_led(void *pvParameter){
    //Especifica que vai ser um GPIO 
     esp_rom_gpio_pad_select_gpio (GPIO_NUM_32);
     esp_rom_gpio_pad_select_gpio (GPIO_NUM_33);

    //Define como saída
     gpio_set_direction (GPIO_NUM_32, GPIO_MODE_OUTPUT);
     gpio_set_direction (GPIO_NUM_33, GPIO_MODE_OUTPUT);

    printf ("GPIO Configurado\n");
    int uiCounter = 0;

    for(;;){
        gpio_set_level(GPIO_NUM_32, (uiCounter%2));
        gpio_set_level(GPIO_NUM_33, ((uiCounter+1)%2));
        vTaskDelay(pdMS_TO_TICKS(10000));
        uiCounter++;
        printf ("Pisquei\n");
    }

} 
void app_main(void) {
    // Inicializa o Wi-Fi
     xTaskCreate(vtask_blink_led     ,
                "vtask_blink_led"   ,
                2048                ,
                NULL                ,
                10                   ,
                NULL);

    printf ("Task Blink Criada");

    wifi_init();


   
    while (1){

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}