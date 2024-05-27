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
#include "driver/ledc.h"

#include <driver/adc.h>
#include <esp_adc_cal.h>

#define WIFI_SSID      "Daiara"
#define WIFI_PASS      "day12358"
#define MAX_HTTP_RECV_BUFFER 1024
#define MAX_COMMAND_LENGTH 1024
#define ESP_INTR_FLAG_DEFAULT 0
#define DEBOUNCE_DELAY_MS 200

esp_adc_cal_characteristics_t adc_cal;//Estrutura que contem as informacoes para calibracao

static const char *TAG = "wifi_http_example";

static bool http_get_task_running = false;


QueueHandle_t queue_tempo;
QueueHandle_t commandQueue;


int statusLed1 = -1; 
int statusLed2 = -1;
int statusLed3 = -1; 
int statusLed4 = -1;
int statusLed5 = -1; 
int statusLed6 = -1;
int statusLed7 = -1; 
int statusLed8 = -1;
int statusLed9 = -1;
int statusLed10 = -1;
int statusLed11 = -1;
int statusLed12 = -1;

int PWM_led2 = 1023;
int PWM_motor_02 = 1023; // esquerda
int dif_pwm = 7;
int PWM_motor_01 = 1016; //direita


gpio_num_t gpio_to_reset = GPIO_NUM_25; //PINO D25
gpio_num_t gpio_to_sobra = GPIO_NUM_26; //PINO D26

gpio_num_t gpio_01 = GPIO_NUM_12; //PINO D12 - wifi
gpio_num_t gpio_02 = GPIO_NUM_13; //PINO D13 - LED COM PWM
gpio_num_t gpio_03 = GPIO_NUM_14; //PINO D14
gpio_num_t gpio_04 = GPIO_NUM_23; //PINO D23
gpio_num_t gpio_05 = GPIO_NUM_22; //PINO D22
gpio_num_t gpio_06 = GPIO_NUM_21; //PINO D21
gpio_num_t gpio_07 = GPIO_NUM_19; //PINO D19
gpio_num_t gpio_08 = GPIO_NUM_18; //PINO D18 //M1 HIGH
gpio_num_t gpio_09 = GPIO_NUM_16; //PINO RX2 //M1_LOW
gpio_num_t gpio_10 = GPIO_NUM_17; //PINO TX2 // M2_HIGH
gpio_num_t gpio_11 = GPIO_NUM_4; //PINO D4 //M2_LOW
gpio_num_t gpio_12 = GPIO_NUM_27; //PINO D27

#define LEDC_TIMER_10_BIT  10
#define LEDC_BASE_FREQ     5000

void setup_pwm_timer() {
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_10_BIT, // Resolução de 13 bits
        .freq_hz = LEDC_BASE_FREQ,            // Frequência de 5 kHz
        .speed_mode = LEDC_HIGH_SPEED_MODE,   // Modo de alta velocidade
        .timer_num = LEDC_TIMER_0             // Timer 0
    };
    ledc_timer_config(&ledc_timer);
}


SemaphoreHandle_t debounceSemaphore;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
      
        xSemaphoreGiveFromISR(debounceSemaphore, NULL);
        
}

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

void post_finished_to_firebase(const char* message) {
    const char* url = "https://carrinhoeducativo-default-rtdb.firebaseio.com/result.json";
    char post_data[256];

    //const char* post_data = "{\"message\": \"Terminei de processar todos os comandos.\"}";
    snprintf(post_data, sizeof(post_data), "{\"message\": \"%s\"}", message);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT, //Se usar POST ele cria um novo ID pra cada requisição
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return;
    }

    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            ESP_LOGI(TAG, "Successfully posted status to Firebase.");
        } else {
            ESP_LOGE(TAG, "HTTP POST failed with status code: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}


void http_get_task(void *pvParameters)
{

    char output_buffer[MAX_HTTP_RECV_BUFFER] = {0};  // Buffer to store response of http request
    esp_http_client_config_t config = {
        .url = "https://carrinhoeducativo-default-rtdb.firebaseio.com/code.json", // Substitua pela sua URL do Firebase
        .event_handler = _http_event_handler,
        .skip_cert_common_name_check = true, //ignorar certificado por enquanto
        .user_data = output_buffer  // Pass a pointer to a buffer to store the response
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

        esp_http_client_cleanup(client);
        // necessario pq se nao depois de um tempo nao consigo fazer novas requisições
        client = esp_http_client_init(&config);

        memset(output_buffer, 0, sizeof(output_buffer));

        ESP_LOGI(TAG, "Botão pressionado, fazendo solicitação HTTP...");
        // Realiza a solicitação HTTP
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
         ESP_LOGI(TAG, "Got data: %s", output_buffer);
          post_finished_to_firebase("Comandos Recebidos");
        if (xQueueSend(commandQueue, &output_buffer, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to send command to the queue");
            }

        } else {
            ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        } 
        

    esp_http_client_cleanup(client);

    http_get_task_running = false; // Indica que a task completou sua execução
    vTaskDelete(NULL); // Termina a tarefa depois de executar a requisição HTTP
    }


    

void debounce_task(void* arg) {
    int pin = (int) arg;

    for(;;) {
        if (xSemaphoreTake(debounceSemaphore, portMAX_DELAY) == pdTRUE) {
            // Aguarda um tempo fixo para estabilizar o sinal
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_DELAY_MS));

            // Verifica se o botão ainda está pressionado (nível baixo)
            if (gpio_get_level(gpio_to_reset) == 0) {
                printf("Botão pressionado com sucesso.\n");
                if(http_get_task_running == false){
                    xTaskCreate(http_get_task, "http_get_task", 4096, (void*)pin, 10, NULL);
                }
                  
            }
        }
     vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_DELAY_MS));
    }
}



void setup_gpio_interrupt() {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE, // Interrupt on falling edge
        .pin_bit_mask = (1ULL << gpio_to_reset), // Bitmask for the pin, using the defined gpio_to_reset
        .mode = GPIO_MODE_INPUT,        // Set as Input
        .pull_up_en = 1,                // Enable pull-up resistor
        .pull_down_en = 0               // Disable pull-down resistor
    };
    gpio_config(&io_conf);

    // Install GPIO interrupt service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    // Attach the interrupt service routine
    gpio_isr_handler_add(gpio_to_reset, gpio_isr_handler, (void*) gpio_to_reset);
}


void config_entradas(){

    esp_rom_gpio_pad_select_gpio (gpio_to_reset);
    esp_rom_gpio_pad_select_gpio (gpio_to_sobra);

    //Define como entrada
     gpio_set_direction (gpio_to_reset, GPIO_MODE_INPUT);
     gpio_set_direction (gpio_to_sobra, GPIO_MODE_INPUT);

    //PullUp para acionar quando lvl = gnd
     gpio_set_pull_mode(gpio_to_reset, GPIO_PULLUP_ONLY);
      gpio_set_pull_mode(gpio_to_sobra, GPIO_PULLUP_ONLY);

}

void config_adc(){
    adc1_config_width(ADC_WIDTH_BIT_12);//Configura a resolucao
        esp_adc_cal_value_t adc_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_cal);//Inicializa a estrutura de calibracao
 
    if (adc_type == ESP_ADC_CAL_VAL_EFUSE_VREF)
    {
        ESP_LOGI("ADC CAL", "Vref eFuse encontrado: V");
    }
    else if (adc_type == ESP_ADC_CAL_VAL_EFUSE_TP)
    {
        ESP_LOGI("ADC CAL", "Two Point eFuse encontrado");
    }
    else
    {
        ESP_LOGW("ADC CAL", "Nada encontrado, utilizando Vref padrao: V");
    }
    
}

void config_saidas(){

     ESP_LOGI("CONFIG SAIDAS", "Iniciei funcao");

    esp_rom_gpio_pad_select_gpio (gpio_01);
    esp_rom_gpio_pad_select_gpio (gpio_02);
    esp_rom_gpio_pad_select_gpio (gpio_03);
    esp_rom_gpio_pad_select_gpio (gpio_04);
    esp_rom_gpio_pad_select_gpio (gpio_05);
    esp_rom_gpio_pad_select_gpio (gpio_06);
    esp_rom_gpio_pad_select_gpio (gpio_07);
    esp_rom_gpio_pad_select_gpio (gpio_08);
    esp_rom_gpio_pad_select_gpio (gpio_09);
    esp_rom_gpio_pad_select_gpio (gpio_10);
    esp_rom_gpio_pad_select_gpio (gpio_11);
    esp_rom_gpio_pad_select_gpio (gpio_12);

      ESP_LOGI("CONFIG SAIDAS", "Finalizei SELECT PIO");

    //Define como saída
     gpio_set_direction (gpio_01, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_02, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_03, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_04, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_05, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_06, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_07, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_08, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_08, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_09, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_10, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_11, GPIO_MODE_OUTPUT);
     gpio_set_direction (gpio_12, GPIO_MODE_OUTPUT);
     
    ESP_LOGI("CONFIG SAIDAS", "Finalizei funcao");

}


    

void activate_pwm(int choice, int pwm) {
    ledc_channel_t channel;
    gpio_num_t gpio_to_use;

    //2 - LED 2
    //8 M1_HIGH
    //9 M1_LOW
    //10 M2_HIGH
    //11 M2_LOW
    // Configuração inicial para determinar qual canal e GPIO serão usados
    switch (choice) {
        case 2:
            channel = LEDC_CHANNEL_0;
            gpio_to_use = gpio_02; // Exemplo de GPIO
            statusLed2 = 1;
            break;
        case 8:
            channel = LEDC_CHANNEL_1;
            gpio_to_use = gpio_08; // Exemplo de GPIO
            statusLed8 = 1;
            break;
        case 9:
            channel = LEDC_CHANNEL_2;
            gpio_to_use = gpio_09; // Exemplo de GPIO
            statusLed9 = 1;
            break;
        case 10:
            channel = LEDC_CHANNEL_3;
            gpio_to_use = gpio_10; // Exemplo de GPIO
            statusLed10 = 1;
            break;
        case 11:
            channel = LEDC_CHANNEL_4;
            gpio_to_use = gpio_11; // Exemplo de GPIO
            statusLed11 = 1;
            break;
        
        
        // Você pode adicionar mais cases aqui para outros canais
        default:
            ESP_LOGE("GPIO", "Seleção de gpiout inválida");
            return; // Sair da função se a escolha for inválida
    }

    // Configurar o canal PWM para o GPIO e canal escolhido
    ledc_channel_config_t ledc_channel = {
        .channel    = channel,
        .duty       = 0,
        .gpio_num   = gpio_to_use,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_0
    };
    ledc_channel_config(&ledc_channel);

    // Definir e atualizar o duty cycle para o canal especificado
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, channel, pwm);
    ESP_LOGI("PWM","ativei duty");
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, channel);
}


void desactivate_pwm(int choice) {
    ledc_channel_t channel;

   gpio_num_t gpio_to_use;
    // Define qual canal será utilizado, baseado na escolha
    switch (choice) {
        case 2:
            channel = LEDC_CHANNEL_0;
            gpio_to_use = gpio_02; // Exemplo de GPIO
            statusLed2 = 0;
            break;
        case 8:
            channel = LEDC_CHANNEL_1;
            gpio_to_use = gpio_08; // Exemplo de GPIO
            statusLed8 = 0;
            break;
        case 9:
            channel = LEDC_CHANNEL_2;
            gpio_to_use = gpio_09; // Exemplo de GPIO
            statusLed9 = 0;
            break;
        case 10:
            channel = LEDC_CHANNEL_3;
            gpio_to_use = gpio_10; // Exemplo de GPIO
            statusLed10 = 0;
            break;
        case 11:
            channel = LEDC_CHANNEL_4;
            gpio_to_use = gpio_11; // Exemplo de GPIO
            statusLed11 = 0;
            break;
        default:
            ESP_LOGE("GPIO", "Seleção de gpiout inválida");
            return;
    }

    // Desativa o PWM definindo o ciclo de trabalho para 0
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, channel, 0);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, channel);
}


void controle_motor(int tempo, int comando){
/*
gpio_num_t gpio_08 = GPIO_NUM_18; //PINO D18 //M1 HIGH
gpio_num_t gpio_09 = GPIO_NUM_16; //PINO RX2 //M1_LOW
gpio_num_t gpio_10 = GPIO_NUM_17; //PINO TX2 // M2_HIGH
gpio_num_t gpio_11 = GPIO_NUM_4; //PINO D4 //M2_LOW*/
     int M1_HIGH = 8 ;
     int M1_LOW =  9;
     int M2_HIGH = 10;
     int M2_LOW = 11;
     int temp;

    //0 - desligar todos as saidas com 0 
    //1 - frente - m1_high e m2_high com 1 e m1_low 0 m2_low 0;
    //2 - direita - m1_high em 1 e demais em 0
    //3 - esquerda - m2 high em 1 e demais em 0
    //4 - tras - m1_low e m2_low em 1 e demais em 0 
    
    switch (comando) {
            case 0:
                desactivate_pwm(M1_HIGH);
                desactivate_pwm(M1_LOW);
                desactivate_pwm(M2_HIGH);
                desactivate_pwm(M2_LOW);
                break;
            case 1:
                activate_pwm(M1_HIGH,PWM_motor_01);
                desactivate_pwm(M1_LOW);
                activate_pwm(M2_HIGH,PWM_motor_02);
                desactivate_pwm(M2_LOW); 
                break;
            case 2:
                activate_pwm(M1_HIGH,PWM_motor_01);
                desactivate_pwm(M1_LOW); 
                desactivate_pwm(M2_HIGH); 
               activate_pwm(M2_LOW,PWM_motor_02);
                break;
            case 3:
                desactivate_pwm(M1_HIGH);
                activate_pwm(M1_LOW,PWM_motor_01);
                activate_pwm(M2_HIGH,PWM_motor_02);
                desactivate_pwm(M2_LOW);
                break;
            case 4:
                
                temp = PWM_motor_02;
                PWM_motor_02 = PWM_motor_01;
                PWM_motor_01 = temp;
                desactivate_pwm(M1_HIGH);
                activate_pwm(M1_LOW,PWM_motor_01); 
                desactivate_pwm(M2_HIGH); 
                activate_pwm(M2_LOW,PWM_motor_02);
                temp = PWM_motor_02;
                PWM_motor_02 = PWM_motor_01;
                PWM_motor_01 = temp;

                break;
           
            default:
                ESP_LOGE("GPIO", "Seleção de gpiout inválida");
                return;  
        }
        int time_efetivo = tempo;

        if(comando == 2 || comando ==3){
            switch (PWM_motor_02)
            {
            case 1023: // PWM em 100%
                time_efetivo = tempo;        
            break;
            case 767: // PWM em 75%
                time_efetivo = (tempo*166)/100;
                break;
            case 512: // PWM em 50%
                time_efetivo = tempo*4;
            break;
            default:
                break;
            }
        }

        ESP_LOGI("tempo efetivo","tempo efetivo %d",time_efetivo);
         sys_delay_ms(time_efetivo);
         desactivate_pwm(M1_HIGH);
         desactivate_pwm(M1_LOW); 
         desactivate_pwm(M2_HIGH);
         desactivate_pwm(M2_LOW);

}



void ativar_out(int seleciona_out){

    gpio_num_t gpio_to_use;
    
    switch (seleciona_out) {
            case 1:
                gpio_to_use = gpio_01;
                statusLed1 =1;
                break;
            case 2:
                gpio_to_use = gpio_02;
                statusLed2 =1;
                break;
            case 3:
                gpio_to_use = gpio_03;
                statusLed3 =1;
                break;
            case 4:
                gpio_to_use = gpio_04;
                statusLed4 =1;
                break;
            case 5:
                gpio_to_use = gpio_05;
                statusLed5 =1;
                break;
            case 6:
                gpio_to_use = gpio_06;
                statusLed6 =1;
                break;
            case 7:
                gpio_to_use = gpio_07;
                statusLed7 =1;
                break;
            case 8:
                gpio_to_use = gpio_08;
                statusLed8 =1;
                break;
            case 9:
                gpio_to_use = gpio_09;
                statusLed9 =1;
                break;
            case 10:
                gpio_to_use = gpio_10;
                statusLed10 =1;
                break;
            case 11:
                gpio_to_use = gpio_11;
                statusLed11 =1;
                break;
            case 12:
                gpio_to_use = gpio_12;
                statusLed12 =1;
                break;
            default:
                ESP_LOGE("GPIO", "Seleção de gpiout inválida");
                return;  
        }

        if(gpio_to_use == gpio_02){
            ESP_LOGI("PWM", "Antes Activate");
            activate_pwm(2,PWM_led2);
        }
        else{
            gpio_set_level(gpio_to_use, 1); // Assume que "1" ativa e "0" desativa    
        }
           

}

void desativar_out(int seleciona_out){

    gpio_num_t gpio_to_use;
    
    switch (seleciona_out) {
            case 1:
                gpio_to_use = gpio_01;
                statusLed1 =0;
                break;
            case 2:
                gpio_to_use = gpio_02;
                statusLed2 =0;
                break;
            case 3:
                gpio_to_use = gpio_03;
                statusLed3 =0;
                break;
            case 4:
                gpio_to_use = gpio_04;
                statusLed4 =0;
                break;
            case 5:
                gpio_to_use = gpio_05;
                statusLed5 =0;
                break;
            case 6:
                gpio_to_use = gpio_06;
                statusLed6 =0;
                break;
            case 7:
                gpio_to_use = gpio_07;
                statusLed7 =0;
                break;
            case 8:
                gpio_to_use = gpio_08;
                statusLed8 =0;
                break;
            case 9:
                gpio_to_use = gpio_09;
                statusLed9 =0;
                break;
            case 10:
                gpio_to_use = gpio_10;
                statusLed10 =0;
                break;
            case 11:
                gpio_to_use = gpio_11;
                statusLed11 =0;
                break;
            case 12:
                gpio_to_use = gpio_12;
                statusLed12 =0;
                break;
            default:
                ESP_LOGE("GPIO", "Seleção de gpiout inválida");
                return;  
        }
        
        if(gpio_to_use == gpio_02){
              ESP_LOGI("PWM", "Antes deActivate");
            desactivate_pwm(2);
        }
        else{
            gpio_set_level(gpio_to_use, 0); // Assume que "1" ativa e "0" desativa    
        }   

}

//aparentemente nao precisava dessa funcao... verificar depois
int status_out(int porta){
    int status = -1;
    switch (porta) {
    case 1:
        status = statusLed1;
        break;
    case 2:
        status = statusLed2;
        break;
    default:
       ESP_LOGI(TAG, "Porta Invalida");
        break;
    }
    return status;

}



void post_firebase_sensor(int seleciona_sensor, const char* message){
    char url[256];  // Aumente o tamanho se necessário
    char chave[32];
    char post_data[256];

    switch (seleciona_sensor) {
    case 1:
        strcpy(chave, "sensor1");
        break;
    case 2:
        strcpy(chave, "sensor2");
        break;
    case 3:
        strcpy(chave, "sensor3");
        break;
    case 4:
        strcpy(chave, "sensor4");
        break;
    case 5:
        strcpy(chave, "sensor5");
        break;
    case 6:
        strcpy(chave, "sensor6");
        break;
    default:
        ESP_LOGE("POST SENSOR", "Sensor não suportado");
        return;
    }

    // Ajusta a URL para apontar para a chave específica do sensor
    snprintf(url, sizeof(url), "https://carrinhoeducativo-default-rtdb.firebaseio.com/sensores/%s.json", chave);

    // Formatar em JSON
    snprintf(post_data, sizeof(post_data), "\"%s\"", message);
    ESP_LOGI("POST SENSOR", "post_data: %s", post_data);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT, // PUT para atualizar apenas o campo específico
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE("POST SENSOR", "Failed to initialize HTTP client");
        return;
    }

    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            ESP_LOGI("POST SENSOR", "Successfully posted status to Firebase sensor POST DATA: %s", post_data);
        } else {
            ESP_LOGE("POST SENSOR", "HTTP POST Sensor failed with status code: %d", status_code);
        }
    } else {
        ESP_LOGE("POST SENSOR", "HTTP POST Sensor request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}
void post_firebase_led(int seleciona_sensor){
    
}



// Handler para eventos do Wi-Fi
void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Tentando reconectar ao AP...");
        if(statusLed1 ==0 || statusLed1 == -1){
            ativar_out(1);
        }
        else if (statusLed1 ==1){
            desativar_out(1);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Conectado com sucesso!");
         ativar_out(1);
        // Chama a tarefa HTTP após a conexão ser estabelecida
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

void setar_pwm(int seleciona, int value){
    //seleciona = 2 led 2
    //seleciona = 3 motor 1
    //seleciona = 4 motor 2
    //seleciona = 5 motor 1 e 2 
    switch (seleciona)
    {
    case 2:
        PWM_led2 = value;
        break;
    case 5:
        PWM_motor_02 = value;
        PWM_motor_01 = value - dif_pwm;
        break;
    
    default:
        break;
    }
}

int ler_adc(int seleciona_sensor){

        int voltage = 0;
        // Escolhendo o canal baseado no sensor selecionado
        adc1_channel_t adc_channel;
        switch (seleciona_sensor) {
            case 1:
                adc_channel = ADC1_CHANNEL_0;
                break;
            case 2:
                adc_channel = ADC1_CHANNEL_3;
                break;
            case 3:
                adc_channel = ADC1_CHANNEL_6;
                break;
            case 4:
                adc_channel = ADC1_CHANNEL_7;
                break;
            case 5:
                adc_channel = ADC1_CHANNEL_4;
                break;
            case 6:
                adc_channel = ADC1_CHANNEL_5;
                break;
            default:
                ESP_LOGE("ADC", "Seleção de sensor inválida");
                return -1;  // Retornar -1 ou outro código de erro apropriado
        }


       
       /* for (int i = 0; i < 10; i++){
            voltage += adc1_get_raw(adc_channel);//Obtem o valor RAW do ADC
            sys_delay_ms(10);
        }*/
         voltage = adc1_get_raw(adc_channel);//Obtem o valor RAW do ADC
        voltage /= 100;
        voltage = esp_adc_cal_raw_to_voltage(voltage, &adc_cal);//Converte e calibra o valor lido (RAW) para mV
        ESP_LOGI("ADC CAL", "SENSOR %d Read mV: %d",seleciona_sensor, voltage);//Mostra a leitura calibrada no Serial Monitor
         char valor_lido[256]; // Ajuste o tamanho conforme necessário
        snprintf(valor_lido, sizeof(valor_lido), "%d", voltage);
        
       // post_firebase_sensor(seleciona_sensor,valor_lido);
       
        return voltage;
    
}


void verifica_condicoes(const char* command, int value, char compara, char**condicoes,int* value_condicoes){
        //post_finished_to_firebase(message);
        int flag_condicoes = 0;
        int num_condicoes = 2;
        //int value_condicoes[num_condicoes];
    ESP_LOGI(TAG, "Condicoes dentro do verifica --- Cond1 %s Cond2 %s",  condicoes[0],condicoes[1]);
    if(strncmp( condicoes[0], "LED",3) == 0|| strncmp( condicoes[1], "LED",3) == 0){
        int selecionaLed;
        int status_seleciona;
        if(strncmp( condicoes[0], "LED",3) == 0){
             sscanf( condicoes[0], "LED*%d",&selecionaLed);
        }
        else if (strncmp( condicoes[1], "LED",3) == 0){
             sscanf( condicoes[1], "LED*%d",&selecionaLed);
        }

        ESP_LOGI(TAG, "LEDSelecionado: %d", selecionaLed);
        status_seleciona = status_out(selecionaLed);
         ESP_LOGI(TAG, "LED  %d Status %d ", selecionaLed,status_seleciona);
        
        if(flag_condicoes<num_condicoes){
        value_condicoes[flag_condicoes] = status_seleciona;
        flag_condicoes++;
        
        }

    }

    //STATUS É ATIVADO 1 OU DESATIVADO 0 
    if(strncmp( condicoes[0], "STATUS",6) == 0 || strncmp( condicoes[1], "STATUS",6) == 0){
        int status_seleciona;
        if(strncmp( condicoes[0], "STATUS",6) == 0){
              sscanf( condicoes[0], "STATUS*%d",&status_seleciona);
        }
        if(strncmp( condicoes[1], "STATUS",6) == 0){
              sscanf( condicoes[1], "STATUS*%d",&status_seleciona);
        }
         ESP_LOGI(TAG, "STATUS SELECIONA  %d  ",status_seleciona);
        if(flag_condicoes<num_condicoes){
        value_condicoes[flag_condicoes] = status_seleciona;
        flag_condicoes++;
        
        }
    }

    //SENSOR É A ENTRADA DO ADC
    if(strncmp( condicoes[0], "SENSOR",6) == 0 || strncmp( condicoes[1], "SENSOR",6) == 0){
        int status_seleciona;

        if(strncmp( condicoes[0], "SENSOR",6) == 0){
              sscanf( condicoes[0], "SENSOR*%d",&status_seleciona);
        }
        if(strncmp( condicoes[1], "SENSOR",6) == 0){
              sscanf( condicoes[1], "SENSOR*%d",&status_seleciona);
        }
         ESP_LOGI(TAG, "SENSOR SELECIONA  %d  ",status_seleciona);
        if(flag_condicoes<num_condicoes){

        value_condicoes[flag_condicoes] = ler_adc(status_seleciona); //melhorar funcao ler_adc
                
         ESP_LOGI(TAG, "VALOR condicoes flag  %d == %d ",flag_condicoes,value_condicoes[flag_condicoes]);
        flag_condicoes++;
           ESP_LOGI(TAG, "Flag condicoes  %d ",flag_condicoes);
        
        }
    }

        //SENSOR É A ENTRADA DO ADC
        //sscanf(input, "%*[^=]=VALOR*%f", &valor) == 1 %*[^=] é um especificador de formato que lê e ignora caracteres até encontrar um sinal de igual =. O asterisco (*) indica que esses caracteres devem ser ignorados (não armazenados).
    if(strncmp( condicoes[0], "VALOR",5) == 0 || strncmp( condicoes[1], "VALOR",5) == 0){
        int MAX_DIGITS = 10;
        int valor_fatorado[MAX_DIGITS];
        int status_seleciona;
        const char* prefix = "VALOR*";
        char* start =""; 
        if(strncmp( condicoes[0], "VALOR",5) == 0){
              start =  strstr(condicoes[0], prefix);
              sscanf( condicoes[0], "VALOR*%d",&status_seleciona);
        }
        if(strncmp( condicoes[1], "VALOR",5) == 0){
              start =  strstr(condicoes[1], prefix);
              sscanf( condicoes[1], "VALOR*%d",&status_seleciona);
        }


         ESP_LOGI(TAG, "VALOR   %d  flag condicoes %d",status_seleciona,flag_condicoes);
          //ESP_LOGI(TAG, "VALOR FATORA   i 0- %d i1- %d i2- %d i3- %d  i4- %d   ", valor_fatorado[0],valor_fatorado[1],valor_fatorado[2],valor_fatorado[3],valor_fatorado[4]);
        if(flag_condicoes<num_condicoes){
        value_condicoes[flag_condicoes] = status_seleciona; 
        ESP_LOGI(TAG, "VALOR condicoes flag  %d == %d ",flag_condicoes,value_condicoes[flag_condicoes]);
        flag_condicoes++;
        ESP_LOGI(TAG, "Flag condicoes  %d ",flag_condicoes);
        
        }
    }


        ESP_LOGI(TAG, "Cond1: %s Valor %d",  condicoes[0],value_condicoes[0]);
        ESP_LOGI(TAG, "Cond2: %s Valor %d",  condicoes[1],value_condicoes[1]);

       // return value_condicoes;
}



int execute_command(const char* command, int value) {
    


    // Preparando a mensagem a ser enviada para o Firebase
    char message[256]; // Ajuste o tamanho conforme necessário
    snprintf(message, sizeof(message), "Comando;%s;%d", command, value);
    
    ESP_LOGI(TAG, "Comando dentro execute: %s %d", command, value);
   //post_finished_to_firebase(message);
   ESP_LOGI("execute command", "comando %s value %d", command,value);

   ESP_LOGI(TAG, "Comando dentro execute: %s", command);
    if (strcmp(command, "ativar") == 0 || strcmp(command, "\"ativar") == 0  ) {
        ativar_out(value);

    } 
    else if (strcmp(command, "mo") == 0 || strcmp(command, "\"mo") == 0  ) {
   
       int valor_sensor = ler_adc(value);
        char valor_lido[256]; // Ajuste o tamanho conforme necessário
        snprintf(valor_lido, sizeof(valor_lido), "%d", valor_sensor);
          ESP_LOGI("monitorar", "Entrei Monitorar com value = %d  valor formato = %s",value,valor_lido);
       post_firebase_sensor(value,valor_lido);

    } 
    
    else if (strcmp(command, "desativar") == 0 || strcmp(command, "\"desativar") == 0  ) {
        desativar_out(value);
     
    } else if (strcmp(command, "esperar") == 0 || strcmp(command, "\"esperar") == 0) {
        vTaskDelay(pdMS_TO_TICKS(value * 1000)); // Espera pelo número especificado de segundos
      
    }
    else if (strcmp(command, "ler") == 0 || strcmp(command, "\"ler") == 0) {
        int voltage = ler_adc(value);
         ESP_LOGI(TAG, "Valor ler ADC: %d", value);
 
    }
    else if (strcmp(command, "pwmled") == 0 || strcmp(command, "\"pwmled") == 0) {
    setar_pwm(2,value);
 
    }
    else if (strcmp(command, "pwmm") == 0 || strcmp(command, "\"pwmm") == 0) {
    setar_pwm(5,value);
 
    }

    else if (strcmp(command, "fre") == 0 || strcmp(command, "\"fre") == 0) {
    controle_motor(value, 1);
 
    }
    else if (strcmp(command, "direita") == 0 || strcmp(command, "\"direita") == 0) {
    controle_motor(value, 2);
 
    }
    else if (strcmp(command, "esquerda") == 0 || strcmp(command, "\"esquerda") == 0) {
    controle_motor(value, 3);
 
    }
    else if (strcmp(command, "tras") == 0 || strcmp(command, "\"tras") == 0) {
    controle_motor(value, 4);
 
    }
    else if (strncmp(command, "se-", 3) == 0  || strncmp(command, "\"se-",4) == 0) {
        char compara = 'a'; // char = < > 
         ESP_LOGI(TAG, "Condição tipo Se");

        char* condicoes[] = {"a","b"};
        
      ESP_LOGI(TAG, "Comando dentro da condição se: %s", command);
        if(strncmp(command, "se-", 3) == 0){
            sscanf(command, "se-%c",&compara);
              ESP_LOGI(TAG, "Compara %c",compara);
            char* condPart = strchr(command + 4, compara);  // Encontra o caracter compara na string
            
            if (condPart) {
                *condPart = '\0';  // Termina a string para isolar a parte antes do '='
                condPart++;  // Move para o caractere após o '='
                char* endPart = strchr(condPart, ';');  // Encontra o ';' na string
                if (endPart) {
                     *endPart = '\0';  // Termina a string para isolar a parte antes do ';'
                 }

            // Agora temos as partes isoladas
            condicoes[0] = command + 4;  // Aponta para o início da condição
            condicoes[1] = condPart;  // Aponta para o início do valor de status
            }
    }
        if(strncmp(command, "\"se-",4) == 0){
             sscanf(command, "se-%c",&compara);
             ESP_LOGI(TAG, "Compara %c",compara);
            char* condPart = strchr(command + 5, compara);  // Encontra o '=' na string
            if (condPart) {
                *condPart = '\0';  // Termina a string para isolar a parte antes do '='
                condPart++;  // Move para o caractere após o '='
                char* endPart = strchr(condPart, ';');  // Encontra o ';' na string
                if (endPart) {
                     *endPart = '\0';  // Termina a string para isolar a parte antes do ';'
                 }

            // Agora temos as partes isoladas
            condicoes[0] = command + 5;  // Aponta para o início da condição
            condicoes[1] = condPart;  // Aponta para o início do valor de status
            }
        }
        int  value_condicoes[2];
        verifica_condicoes(command, value,compara,condicoes,value_condicoes);
        switch (compara)
        {
        case '=':
            if(value_condicoes[0] == value_condicoes[1]){
                return 0;
            }
            else{
                return 10;
            }
            break;
        case '<':
            if(value_condicoes[0] < value_condicoes[1]){
                return 0;
            }
            else{
                return 10;
            }
            break;
        case '>':
            if(value_condicoes[0] > value_condicoes[1]){
                return 0;
            }
            else{
                return 10;
            }
            break;
        case '!':
            if(value_condicoes[0] != value_condicoes[1]){
                return 0;
            }
            else{
                return 10;
            }
            break;
        
        
        default:
            break;
        }


    vTaskDelay(pdMS_TO_TICKS(50));   
    }
    else if (strncmp(command, "while-", 6) == 0  || strncmp(command, "\"while-",7) == 0) {
        char compara = 'a'; // char = < > 
         ESP_LOGI(TAG, "Condição tipo WHILE");

        char* condicoes[] = {"a","b"};
        
      
        if(strncmp(command, "while-", 6) == 0){
            sscanf(command, "while-%c",&compara);
              ESP_LOGI(TAG, "Compara %c",compara);
            char* condPart = strchr(command + 7, compara);  // Encontra o caracter compara na string
            
            if (condPart) {
                *condPart = '\0';  // Termina a string para isolar a parte antes do '='
                condPart++;  // Move para o caractere após o '='
                char* endPart = strchr(condPart, ';');  // Encontra o ';' na string
                if (endPart) {
                     *endPart = '\0';  // Termina a string para isolar a parte antes do ';'
                 }

            // Agora temos as partes isoladas
            condicoes[0] = command + 7;  // Aponta para o início da condição
            condicoes[1] = condPart;  // Aponta para o início do valor de status
            }
    }
        if(strncmp(command, "\"while-",7) == 0){
             sscanf(command, "se-%c",&compara);
             ESP_LOGI(TAG, "Compara %c",compara);
            char* condPart = strchr(command + 8, compara);  // Encontra o '=' na string
            if (condPart) {
                *condPart = '\0';  // Termina a string para isolar a parte antes do '='
                condPart++;  // Move para o caractere após o '='
                char* endPart = strchr(condPart, ';');  // Encontra o ';' na string
                if (endPart) {
                     *endPart = '\0';  // Termina a string para isolar a parte antes do ';'
                 }

            // Agora temos as partes isoladas
            condicoes[0] = command + 8;  // Aponta para o início da condição
            condicoes[1] = condPart;  // Aponta para o início do valor de status
            }
        }
        int  value_condicoes[2];
        verifica_condicoes(command, value,compara,condicoes,value_condicoes);
        ESP_LOGI("While", "cond1 =%s valor =%d   cond 2 =%s   valor =%d ",condicoes[0],value_condicoes[0],condicoes[1],value_condicoes[1]);
        switch (compara)
        {
        case '=':
            if(value_condicoes[0] == value_condicoes[1]){
                return 20; //para entrar no verifica while 
            }
            else{
                return 21; //para eliminar proximas 
            }
            break;
        case '<':
            if(value_condicoes[0] < value_condicoes[1]){
                return 20;
            }
            else{
                return 21; //para eliminar proximas 
            }
            break;
        case '>':
        ESP_LOGI("While","Entrei maior");
            if(value_condicoes[0] > value_condicoes[1]){
                ESP_LOGI("While","No maior, condição verdadeira, retornando 20");
                return 20;
            }
            else{
                              ESP_LOGI("While","No maior, condição falsa, retornando 21");
                return 21; //para eliminar proximas 
            }
            break;
        case '!':
            if(value_condicoes[0] != value_condicoes[1]){
                return 20;
            }
            else{
                return 21; //para eliminar proximas 
            }
            break;
        
        default:
            break;
        }



}
return 0;
}
void process_commands_while(const char* commands_while, int n_comandos, int qnt_strtok_while) {
   
       
    static char* current_position_while = NULL; // Para manter a posição atual entre chamadas
    
    current_position_while = strdup(commands_while);
    ESP_LOGI("While", "Current position do while: %s", current_position_while);
    char* token_while = strtok(current_position_while, "\\n");
    ESP_LOGI("While", "Token do while antes de ajustar: %s e n_comandos; %d  e qnt_strtok = %d", token_while,n_comandos,qnt_strtok_while);
    for (int i = 0; i <= qnt_strtok_while; i++) // = pra pular o while junto
    {
       token_while = strtok(NULL, "\\n");
    }
    
    
    int verifica_while = 0; // Valor para avaliar if, else e while

    while (token_while != NULL&&n_comandos!=0) {
        char command_while[30];
        int value_while;
        sscanf(token_while, "%[^;];%d", command_while, &value_while);
         ESP_LOGI("While", "Comando no process command do while: %s", command_while);

        verifica_while = execute_command(command_while, value_while);
        if(verifica_while==0){ // execucao normal 
         token_while = strtok(NULL, "\\n");
         n_comandos--;

        }
        else if(verifica_while==10){ //10 = remove comandos. 
            for(int i = 0; i<=value_while;i++){
            
                 token_while = strtok(NULL, "\\n");
                 n_comandos--;
                   ESP_LOGI(TAG, "Removendo comando do while %d",i);
                            
            }
        }

        else if(verifica_while==20){//20 = WHILE 
            ESP_LOGI(TAG, "Entrei num While dentro de um While");
           
        }     


    }
   
     ESP_LOGI("While", "Terminei de processar todos os comandos do WHILE");
        free(current_position_while);
        current_position_while = NULL;
   
}


void process_commands(const char* commands) {
   

    int qnt_strtok = 0;
    int temp_qnt_strtok =0;
    
    static char* current_position = NULL; // Para manter a posição atual entre chamadas
    
    current_position = strdup(commands);
    

    char* token = strtok(current_position, "\\n");

    int verifica = 0; // Valor para avaliar if, else e while

    while (token != NULL) {
        
        char command[30];
        int value;
        sscanf(token, "%[^;];%d", command, &value);
         ESP_LOGI(TAG, "Comando no process command: %s", command);

        verifica = execute_command(command, value);
        if(verifica==0){ // execucao normal 
         token = strtok(NULL, "\\n");
         qnt_strtok ++;
         ESP_LOGI("qnt_strtok", " qnt_strtok apos execucao normal verifica == 0  = %d", qnt_strtok);

        }
        else if(verifica==10){ //10 = remove comandos. 
            for(int i = 0; i<=value;i++){
            
                 token = strtok(NULL, "\\n");
                
                 qnt_strtok ++;
                 ESP_LOGI("qnt_strtok", " qnt_strtok apos somar verifica == 10  = %d", qnt_strtok);
                ESP_LOGI(TAG, "Removendo comando %d",i);
                            
            }
        }

        else if(verifica==20){//20 = WHILE 
            ESP_LOGI(TAG, "----------------------------------------------------------------------------");
             ESP_LOGI("while process comand", "process comando verifica = 20 ");
            static char* commands_while = NULL; // Para manter a posição atual entre chamadas
            commands_while = strdup(commands);

            process_commands_while (commands,value,qnt_strtok);

            free(commands_while);
            commands_while = NULL;
            ESP_LOGI(TAG, "----------------------------------------------------------------------------");
        }
      
        else if(verifica==21){
             ESP_LOGI("while process comand", "process comando verifica = 21 ");
                current_position = strdup(commands);
                token = strtok(current_position, "\\n");
                temp_qnt_strtok = value +1;
                 ESP_LOGI("qnt_strtok", " qnt_strtok para temp_qnt_strtok = %d",qnt_strtok);
                    for (int i = 0; i <= qnt_strtok; i++) // = pra pular o while junto
                    {
                        token = strtok(NULL, "\\n");
                        
                    }
                    for (int i = 0; i < value; i++) // = pra pular o while junto
                    {
                        token = strtok(NULL, "\\n");
                    }
                    ESP_LOGI("qnt_strtok", " qnt_strtok antes somar com temp (%d) = %d",temp_qnt_strtok, qnt_strtok);
                    qnt_strtok = qnt_strtok + temp_qnt_strtok;
                    ESP_LOGI("qnt_strtok", " qnt_strtok depois somar com temp (%d) = %d",temp_qnt_strtok, qnt_strtok);

        }
       vTaskDelay(10); //necessario para nao estourar o watchdog
    }
      post_finished_to_firebase("Finalizado");
     ESP_LOGI(TAG, "Terminei de processar todos os comandos.");
        free(current_position);
        current_position = NULL;
   
}

//TEST GIT

void commandTask(void *pvParameters) {
    char receivedCommand[MAX_COMMAND_LENGTH];

    
    while (1) {
        if (xQueueReceive(commandQueue, &receivedCommand, portMAX_DELAY) == pdPASS) {
            // Processa o comando recebido
            process_commands(receivedCommand);
        }
    }
}


void app_main(void) {
    ESP_LOGI("MAIN", "Entrei main");
    
    // criar fila 
     queue_tempo = xQueueCreate(10, sizeof(int));
     commandQueue = xQueueCreate(10, sizeof(char) * MAX_COMMAND_LENGTH);
     
    ESP_LOGI("MAIN", "criei task");
     
        // Desativar todos os logs
   // esp_log_level_set("*", ESP_LOG_NONE);

    // Ativar logs somente para a tag ADC_CAL
    //esp_log_level_set("POST SENSOR", ESP_LOG_INFO);


    config_adc();
    ESP_LOGI("MAIN", "configurei adc");
    
    config_saidas();
    ESP_LOGI("MAIN", "configurei saidas");

    config_entradas();
    ESP_LOGI("MAIN", "configurei entradas");

    setup_pwm_timer(); // Configura o timer antes de ativar o PWM


    debounceSemaphore = xSemaphoreCreateBinary();
    xTaskCreate(debounce_task, "debounce_task", 2048, NULL, 10, NULL);

    setup_gpio_interrupt();
    ESP_LOGI("MAIN", "configurei interrupções");

     xTaskCreate(commandTask, "commandTask", 10240, NULL, 15, NULL);

   // Inicializa o Wi-Fi
    wifi_init();


   
    while (1){

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}