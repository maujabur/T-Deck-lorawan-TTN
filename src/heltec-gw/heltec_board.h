#pragma once
//
// heltec_board.h
// --------------
// Pinagem do Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262 + OLED SSD1306).
// Fonte: esquemático oficial Heltec HTIT-WB32LA(F) V3.
//
#include <Arduino.h>

// ---- Rádio SX1262 (barramento SPI dedicado do Heltec) ----
#define HELTEC_LORA_NSS    8    // CS
#define HELTEC_LORA_RST    12   // NRST
#define HELTEC_LORA_DIO1   14   // IRQ
#define HELTEC_LORA_BUSY   13   // BUSY
#define HELTEC_LORA_SCK    9
#define HELTEC_LORA_MISO   11
#define HELTEC_LORA_MOSI   10

// ---- OLED SSD1306 (I2C) ----
#define HELTEC_OLED_SDA    17
#define HELTEC_OLED_SCL    18
#define HELTEC_OLED_RST    21

// ---- Controle de energia dos periféricos (Vext) ----
// Vext liga/desliga a alimentação do OLED (e outros). ATIVO EM NÍVEL BAIXO.
#define HELTEC_VEXT        36

// ---- Parâmetros de rádio ----
// Vêm do header compartilhado para casar EXATAMENTE com o transmissor de
// teste do T-Deck (modo TEST_LORA_TX).
#include "lora_test_params.h"
#define HELTEC_RADIO_FREQ      LORATEST_FREQ
#define HELTEC_RADIO_BW        LORATEST_BW
#define HELTEC_RADIO_SF        LORATEST_SF
#define HELTEC_RADIO_CR        LORATEST_CR
#define HELTEC_RADIO_SYNC      LORATEST_SYNC
#define HELTEC_RADIO_PREAMBLE  LORATEST_PREAMBLE
#define HELTEC_RADIO_TXPOWER   LORATEST_TXPOWER
