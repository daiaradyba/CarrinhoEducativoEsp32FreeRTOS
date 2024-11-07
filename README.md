# Firmware do Carrinho Robótico - ESP32 WROOM

Este repositório contém o firmware desenvolvido para o carrinho robótico controlado por um ESP32 WROOM. O código é programado usando o ESP-IDF (Espressif IoT Development Framework) e utiliza o FreeRTOS para multitarefa, permitindo controle eficiente e comunicação em tempo real com a interface web de programação baseada em blocos (Blockly).

## Principais Funcionalidades

- **Controle de Motores e LEDs**: Gerenciamento de motores para movimentação do carrinho e controle de LEDs indicativos.
- **Leitura de Sensores**: Leitura de sensores de proximidade e sensores de linha, permitindo que o carrinho reaja ao ambiente.
- **Comunicação via Wi-Fi**: Conexão com o Firebase para recebimento e envio de comandos em tempo real.
- **FreeRTOS para Multitarefa**: Uso do sistema operacional em tempo real FreeRTOS, garantindo o controle simultâneo de motores, sensores e comunicação com a interface web.

## Tecnologias Utilizadas

- **ESP-IDF**: Framework de desenvolvimento da Espressif para programação do ESP32.
- **FreeRTOS**: Sistema operacional em tempo real utilizado para multitarefa.
- **Firebase**: Usado para comunicação em tempo real com a interface de controle.
- **Wi-Fi**: Comunicação sem fio para controle e monitoramento do carrinho.

## Estrutura do Projeto

- `main.c`: Código principal do firmware, que configura as tarefas FreeRTOS para controle dos motores, LEDs e leitura de sensores, além de configurar a comunicação via Wi-Fi com o Firebase.
- **Tarefas FreeRTOS**: Configuração de tarefas para controle eficiente de cada funcionalidade:
  - **Tarefa de Movimento**: Gerencia os motores de acordo com os comandos recebidos.
  - **Tarefa de Sensores**: Faz leituras periódicas dos sensores de proximidade e linha.
  - **Tarefa de Comunicação**: Envia e recebe dados do Firebase para atualizar o status em tempo real.
