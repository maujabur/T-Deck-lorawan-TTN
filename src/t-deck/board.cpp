#include "board.h"
#include <SPI.h>
#include <esp_sleep.h>

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

static bool s_blOn = true;

void boardBacklight(bool on) {
  digitalWrite(BOARD_BL_PIN, on ? HIGH : LOW);
  s_blOn = on;
}

bool boardBacklightIsOn() {
  return s_blOn;
}

void boardDeepSleep() {
  // 1) Backlight off.
  boardBacklight(false);

  // 2) Desseleciona os periféricos do bus e corta o trilho de energia. Isso
  //    desliga tela/rádio/SD/I2C (o grosso do consumo). As Tasks não devem
  //    mais tocar o bus depois daqui — quem chama já parou o rádio e está
  //    prestes a dormir.
  digitalWrite(BOARD_TFT_CS,    HIGH);
  digitalWrite(BOARD_RADIO_CS,  HIGH);
  digitalWrite(BOARD_SDCARD_CS, HIGH);
  digitalWrite(BOARD_POWERON, LOW);

  // 3) Deep sleep SEM fonte de wake: só o ciclo da chave física (power-cycle)
  //    religa. Um toque acidental (bolinha) NÃO acorda — proteção real.
  Serial.println("[PWR] Deep sleep de protecao. Religue a chave para voltar.");
  Serial.flush();
  esp_deep_sleep_start();   // não retorna
}
