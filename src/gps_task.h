#pragma once
//
// gps_task.h
// ----------
// Leitura do GPS ATGM336H do T-Deck via Serial1 (UART1 por hardware).
// Pinos: RX = GPIO43, TX = GPIO44 (do ponto de vista do ESP32-S3).
//
#include <Arduino.h>

// Pinos seriais do GPS no T-Deck.
// Mapeamento conforme o firmware de fabrica da LilyGO (utilities.h):
// o ESP32 recebe (RX) no GPIO44 e transmite (TX) no GPIO43.
//
// ATENCAO (hardware desta placa): o TX/RX foi CRUZADO na solda. Essa troca
// fisica, combinada com os pinos abaixo, resulta na comunicacao correta.
// Se um dia a solda for refeita "reta" (sem cruzar), inverta estes dois
// defines de volta para RX=43 / TX=44.
#define GPS_RX_PIN   44   // ESP32 RX  <- GPS TX
#define GPS_TX_PIN   43   // ESP32 TX  -> GPS RX
#define GPS_BAUD     9600 // baud padrão do ATGM336H

// Inicializa a UART do GPS. Chame no setup() antes de criar a Task.
void gpsInit();

// Task que alimenta o parser NMEA e publica a posição em g_state.
void gpsTask(void* pvParameters);
