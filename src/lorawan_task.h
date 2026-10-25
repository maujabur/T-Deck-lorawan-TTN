#pragma once
//
// lorawan_task.h
// --------------
// Scanner LoRaWAN não-bloqueante para a região AU915. Alterna entre
// FSB 2 e FSB 1 a cada 20s enquanto não encontrar um gateway (join).
//
#include <Arduino.h>
#include <RadioLib.h>
#include "board.h"   // BOARD_RADIO_CS e pinos do bus compartilhado

// Pinagem de controle do rádio SX1262 no T-Deck.
// O CS vem do board.h (BOARD_RADIO_CS) para não divergir.
#define LORA_CS        BOARD_RADIO_CS
#define LORA_RST       17
#define LORA_BUSY      13
#define LORA_DIO1      45

// Intervalo entre trocas de sub-banda (ms).
#define SCAN_INTERVAL_MS  20000UL

// Inicializa o rádio (o bus SPI já foi aberto por boardInit()). Retorna o
// código do RadioLib (RADIOLIB_ERR_NONE em caso de sucesso).
int16_t loraInit();

// Task do scanner LoRaWAN.
void loraTask(void* pvParameters);
