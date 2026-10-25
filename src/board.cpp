#include "board.h"
#include <SPI.h>

SemaphoreHandle_t g_spiMutex = nullptr;

void boardInit() {
  // 1) Liga o trilho de energia dos periféricos.
  pinMode(BOARD_POWERON, OUTPUT);
  digitalWrite(BOARD_POWERON, HIGH);
  delay(500);

  // 2) Todos os chip-selects do barramento em nível ALTO (desselecionados)
  //    antes de iniciar o SPI, para nenhum dispositivo responder por engano.
  pinMode(BOARD_TFT_CS,    OUTPUT);
  pinMode(BOARD_RADIO_CS,  OUTPUT);
  pinMode(BOARD_SDCARD_CS, OUTPUT);
  digitalWrite(BOARD_TFT_CS,    HIGH);
  digitalWrite(BOARD_RADIO_CS,  HIGH);
  digitalWrite(BOARD_SDCARD_CS, HIGH);

  // 3) MISO com pull-up (recomendação LilyGO).
  pinMode(BOARD_SPI_MISO, INPUT_PULLUP);

  // 4) NÃO iniciamos a 'SPI' global aqui. O display (TFT_eSPI, porta HSPI)
  //    inicializa o barramento em tft.init() e o rádio reaproveita a mesma
  //    instância. Assim só há UM dono dos pinos SPI 40/41/38.

  // 5) Backlight ligado.
  pinMode(BOARD_BL_PIN, OUTPUT);
  digitalWrite(BOARD_BL_PIN, HIGH);

  // Mutex do bus compartilhado.
  if (g_spiMutex == nullptr) {
    g_spiMutex = xSemaphoreCreateMutex();
  }
}
