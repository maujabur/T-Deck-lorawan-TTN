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

// ---- Parâmetros de rádio (AU915 / banda ISM 915 MHz) ----
// Para teste ponto-a-ponto, rádio e T-Deck precisam usar os MESMOS
// parâmetros (frequência, BW, SF, CR, sync word, preâmbulo).
#define HELTEC_RADIO_FREQ  915.0   // MHz
#define HELTEC_RADIO_BW    125.0   // kHz
#define HELTEC_RADIO_SF    9       // spreading factor
#define HELTEC_RADIO_CR    7       // coding rate 4/7
#define HELTEC_RADIO_SYNC  0x12    // sync word LoRa privado (0x12). TTN usa 0x34.
#define HELTEC_RADIO_PREAMBLE  8
#define HELTEC_RADIO_TXPOWER   10  // dBm
