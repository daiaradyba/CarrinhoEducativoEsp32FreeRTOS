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
#include "freertos/queue.h"

#define WIFI_SSID      "Dyba_Bernardelli_2G"
#define WIFI_PASS      "123581321"
#define MAX_HTTP_RECV_BUFFER 512

static const char *TAG = "wifi_http_example";

QueueHandle_t queue_tempo;

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {

    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read

      switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            // Check for chunked encoding is set to false
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is not chunked
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
    case HTTP_EVENT_REDIRECT:
    
    ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
    break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            if (output_buffer != NULL) {
                // Free memory of output buffer
                free(output_buffer);
                output_buffer = NULL;
                output_len = 0;
            }
            break;
    }
    return ESP_OK;
}

void http_get_task(void *pvParameters)
{

    esp_rom_gpio_pad_select_gpio (GPIO_NUM_25);

    //Define como saída
     gpio_set_direction (GPIO_NUM_25, GPIO_MODE_INPUT);

    //PullUp para acionar quando lvl = gnd
     gpio_set_pull_mode(GPIO_NUM_25, GPIO_PULLUP_ONLY);


    char output_buffer[MAX_HTTP_RECV_BUFFER] = {0};  // Buffer to store response of http request
    esp_http_client_config_t config = {
        .url = "https://carrinhoeducativo-default-rtdb.firebaseio.com/code.json", // Substitua pela sua URL do Firebase
        .event_handler = _http_event_handler,
        .skip_cert_common_name_check = true, //ignorar certificado por enquanto
        .user_data = output_buffer  // Pass a pointer to a buffer to store the response
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    while(1){
        while (gpio_get_level(GPIO_NUM_25) == 1) {
            vTaskDelay(pdMS_TO_TICKS(100)); // Evita checagem contínua, diminuindo o uso da CPU
        }
        // Debounce simples
        vTaskDelay(pdMS_TO_TICKS(50));

        
    if (gpio_get_level(GPIO_NUM_25) == 0) {
        esp_http_client_cleanup(client);
        // necessario pq se nao depois de um tempo nao consigo fazer novas requisições
        client = esp_http_client_init(&config);

            ESP_LOGI(TAG, "Botão pressionado, fazendo solicitação HTTP...");
            // Realiza a solicitação HTTP
            esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
        ESP_LOGI(TAG, "Got data: %s", output_buffer);
        // Parse do JSON para extrair o valor do tempo
        int tempo_piscada;
        sscanf(output_buffer, "\"%d\"", &tempo_piscada);
        printf("Tempo Piscada get task %d\n",tempo_piscada);

        // Limpa o buffer para a próxima leitura
        memset(output_buffer, 0, MAX_HTTP_RECV_BUFFER);

        // Enviar o valor do tempo para a fila

        if (xQueueSend(queue_tempo, &tempo_piscada, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to send to the queue");
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

           
        }

        // Aguarda um momento antes de permitir outra checagem para evitar múltiplas detecções
        vTaskDelay(pdMS_TO_TICKS(500));
    }


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
    int tempo_piscada = 1; // Tempo padrão de piscada em segundos

    for(;;){
        if (xQueueReceive(queue_tempo, &tempo_piscada, 0) == pdPASS) {
            ESP_LOGI(TAG, "Tempo de piscada atualizado para %d segundos", tempo_piscada);
        }
        gpio_set_level(GPIO_NUM_32, (uiCounter%2));
        gpio_set_level(GPIO_NUM_33, ((uiCounter+1)%2));
        vTaskDelay(pdMS_TO_TICKS(tempo_piscada * 1000));
        uiCounter++;
        printf ("Pisquei\n");
    }

} 
void app_main(void) {
    // criar fila 
     queue_tempo = xQueueCreate(10, sizeof(int));
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