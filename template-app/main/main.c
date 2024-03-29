#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    // Loop infinito
    while(1) {
        printf("Mensagem enviada a cada 5 segundos\n");

        // Delay de 5 segundos
        // pdMS_TO_TICKS converte milissegundos em ticks. 5000 milissegundos = 5 segundos
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}