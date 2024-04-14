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

#include <driver/adc.h>
#include <esp_adc_cal.h>

#define WIFI_SSID      "Dyba_Bernardelli_2G"
#define WIFI_PASS      "123581321"
#define MAX_HTTP_RECV_BUFFER 1024
#define MAX_COMMAND_LENGTH 1024

esp_adc_cal_characteristics_t adc_cal;//Estrutura que contem as informacoes para calibracao

static const char *TAG = "wifi_http_example";

QueueHandle_t queue_tempo;
QueueHandle_t commandQueue;

int statusLed1 = -1; 
int statusLed2 = -1;

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


int ler_adc(){
        int voltage = 0;
        for (int i = 0; i < 10; i++){
            voltage += adc1_get_raw(ADC1_CHANNEL_0);//Obtem o valor RAW do ADC
            sys_delay_ms(30);
        }
        voltage /= 100;
        voltage = esp_adc_cal_raw_to_voltage(voltage, &adc_cal);//Converte e calibra o valor lido (RAW) para mV
        ESP_LOGI("ADC CAL", "Read mV: %d", voltage);//Mostra a leitura calibrada no Serial Monitor
         char valor_lido[256]; // Ajuste o tamanho conforme necessário
        snprintf(valor_lido, sizeof(valor_lido), "Leitura;%d", voltage);
        post_finished_to_firebase(valor_lido);
        vTaskDelay(pdMS_TO_TICKS(10));   
        return voltage;
    
}

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
        value_condicoes[flag_condicoes] = ler_adc(); //melhorar funcao ler_adc
        flag_condicoes++;
        
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


         ESP_LOGI(TAG, "VALOR   %d  ",status_seleciona);
          ESP_LOGI(TAG, "VALOR FATORA   i0- %d i1- %d i2- %d i3- %d  i4- %d   ", valor_fatorado[0],valor_fatorado[1],valor_fatorado[2],valor_fatorado[3],valor_fatorado[4]);
        if(flag_condicoes<num_condicoes){
        value_condicoes[flag_condicoes] = status_seleciona; //melhorar funcao ler_adc
        flag_condicoes++;
        
        }
    }


        ESP_LOGI(TAG, "Cond1: %s Valor %d",  condicoes[0],value_condicoes[0]);
        ESP_LOGI(TAG, "Cond2: %s Valor %d",  condicoes[1],value_condicoes[1]);

       // return value_condicoes;
}



int execute_command(const char* command, int value) {
    
     esp_rom_gpio_pad_select_gpio (GPIO_NUM_32);
     esp_rom_gpio_pad_select_gpio (GPIO_NUM_33);

    // Preparando a mensagem a ser enviada para o Firebase
    char message[256]; // Ajuste o tamanho conforme necessário
    snprintf(message, sizeof(message), "Comando;%s;%d", command, value);
    
    ESP_LOGI(TAG, "Comando dentro execute: %s %d", command, value);
   //post_finished_to_firebase(message);

    //Define como saída
     gpio_set_direction (GPIO_NUM_32, GPIO_MODE_OUTPUT);
     gpio_set_direction (GPIO_NUM_33, GPIO_MODE_OUTPUT);

     // Seleciona o GPIO baseado no valor.
    gpio_num_t gpio_to_use = (value == 1) ? GPIO_NUM_32 : GPIO_NUM_33;

   ESP_LOGI(TAG, "Comando dentro execute: %s", command);
    if (strcmp(command, "ativar") == 0 || strcmp(command, "\"ativar") == 0  ) {
        gpio_set_level(gpio_to_use, 1); // Assume que "1" ativa e "0" desativa

         ESP_LOGI(TAG, "Ativei");
         switch (gpio_to_use)
         {
         case GPIO_NUM_32:
            statusLed1 =1;
            break;
         case GPIO_NUM_33:
            statusLed2 =1;
            break;
         
         default:
            break;
         }

    } else if (strcmp(command, "desativar") == 0 || strcmp(command, "\"desativar") == 0  ) {
        gpio_set_level(gpio_to_use, 0); // Assume que "1" desativa e "0" mantém ativado
        switch (gpio_to_use)
         {
         case GPIO_NUM_32:
            statusLed1 =0;
            break;
         case GPIO_NUM_33:
            statusLed2 =0;
            break;
         
         default:
            break;
         }
     
    } else if (strcmp(command, "esperar") == 0 || strcmp(command, "\"esperar") == 0) {
        vTaskDelay(pdMS_TO_TICKS(value * 1000)); // Espera pelo número especificado de segundos
      
    }
    else if (strcmp(command, "ler") == 0 || strcmp(command, "\"ler") == 0) {
        int voltage = ler_adc();
 
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
        
        default:
            break;
        }



}
return 0;
}

void process_commands_while(const char* commands_while, int n_comandos, int qnt_strok_while) {
   
       
    static char* current_position_while = NULL; // Para manter a posição atual entre chamadas
    
    current_position_while = strdup(commands_while);
    ESP_LOGI(TAG, "Current position do while: %s", current_position_while);
    char* token_while = strtok(current_position_while, "\\n");
    for (int i = 0; i <= qnt_strok_while; i++) // = pra pular o while junto
    {
       token_while = strtok(NULL, "\\n");
    }
    
    ESP_LOGI(TAG, "Token do while: %s e n_comandos; %d", token_while,n_comandos);
    int verifica_while = 0; // Valor para avaliar if, else e while

    while (token_while != NULL&&n_comandos!=0) {
        char command_while[30];
        int value_while;
        sscanf(token_while, "%[^;];%d", command_while, &value_while);
         ESP_LOGI(TAG, "Comando no process command do while: %s", command_while);

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
   
     ESP_LOGI(TAG, "Terminei de processar todos os comandos do WHILE");
        free(current_position_while);
        current_position_while = NULL;
   
}


void process_commands(const char* commands) {
   

    int qnt_strtok = 0;
    
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

        }
        else if(verifica==10){ //10 = remove comandos. 
            for(int i = 0; i<=value;i++){
            
                 token = strtok(NULL, "\\n");
                 qnt_strtok ++;
                   ESP_LOGI(TAG, "Removendo comando %d",i);
                            
            }
        }

        else if(verifica==20){//20 = WHILE 
            ESP_LOGI(TAG, "----------------------------------------------------------------------------");
                
            static char* commands_while = NULL; // Para manter a posição atual entre chamadas
            commands_while = strdup(commands);

            process_commands_while (commands,value,qnt_strtok);

            free(commands_while);
            commands_while = NULL;
            ESP_LOGI(TAG, "----------------------------------------------------------------------------");
        }
        else if(verifica==21){
                current_position = strdup(commands);
                token = strtok(current_position, "\\n");
                    for (int i = 0; i <= qnt_strtok; i++) // = pra pular o while junto
                    {
                        token = strtok(NULL, "\\n");
                    }
                    for (int i = 0; i < value; i++) // = pra pular o while junto
                    {
                        token = strtok(NULL, "\\n");
                    }
        }
       
    }
      post_finished_to_firebase("Finalizado");
     ESP_LOGI(TAG, "Terminei de processar todos os comandos.");
        free(current_position);
        current_position = NULL;
   
}

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
    // criar fila 
     queue_tempo = xQueueCreate(10, sizeof(int));
     commandQueue = xQueueCreate(10, sizeof(char) * MAX_COMMAND_LENGTH);
     
    config_adc();
     //xTaskCreate(vtask_blink_led,"vtask_blink_led",2048,NULL,5,NULL);
     xTaskCreate(commandTask, "commandTask", 10240, NULL, 15, NULL);

    printf ("Task Blink Criada");

   // Inicializa o Wi-Fi
    wifi_init();


   
    while (1){

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}