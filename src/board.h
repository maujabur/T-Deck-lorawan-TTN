#pragma once
//
// board.h
// -------
// Bring-up de hardware do LilyGO T-Deck e barramento SPI compartilhado.
//
// IMPORTANTE (causa do Core Panic / StoreProhibited):
//   No T-Deck o display (ST7789), o rádio (SX1262) e o cartão SD dividem
//   o MESMO barramento SPI. A sequência de boot EXIGE, ANTES de usar
//   qualquer periférico:
//     1. Ligar BOARD_POWERON.
//     2. Colocar os 3 chip-selects (TFT/RADIO/SD) em OUTPUT e nível ALTO.
//     3. MISO em INPUT_PULLUP.
//     4. SPI.begin(SCK, MISO, MOSI)  <-- antes de tft.init()!
//   Pular o passo 4 faz o TFT_eSPI chamar spi.beginTransaction() num bus
//   não inicializado -> escrita em ponteiro nulo -> panic.
//
#include <Arduino.h>

// ---- Alimentação dos periféricos ----
#define BOARD_POWERON     10

// ---- Barramento SPI compartilhado (TFT + LoRa + SD) ----
#define BOARD_SPI_SCK     40
#define BOARD_SPI_MISO    38
#define BOARD_SPI_MOSI    41

// ---- Chip-selects dos 3 dispositivos no bus ----
#define BOARD_TFT_CS      12
#define BOARD_RADIO_CS    9
#define BOARD_SDCARD_CS   39

// ---- Backlight do display ----
#define BOARD_BL_PIN      42

// Executa a sequência de bring-up acima. Chame UMA vez no início do setup().
void boardInit();

// Mutex do barramento SPI. Qualquer Task que vá falar com TFT ou rádio
// deve travar isto durante a transação para não colidir no bus.
extern SemaphoreHandle_t g_spiMutex;

static inline void spiBusLock()   { xSemaphoreTake(g_spiMutex, portMAX_DELAY); }
static inline void spiBusUnlock() { xSemaphoreGive(g_spiMutex); }

struct SpiBusGuard {
  SpiBusGuard()  { spiBusLock(); }
  ~SpiBusGuard() { spiBusUnlock(); }
};
