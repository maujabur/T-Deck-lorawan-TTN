//
// main.cpp — T-Deck GPS + LoRaWAN Scanner
// ---------------------------------------
// Orquestra o boot do hardware e cria as três Tasks do FreeRTOS:
//   - uiTask    (core 0): desenha a tela personalizada do T-Deck
//   - gpsTask   (core 0): lê o GPS ATGM336H (Serial1, RX=43/TX=44)
//   - loraTask  (core 1): scanner LoRaWAN AU915 rotacionando FSB2<->FSB1
//
// Toda comunicação entre Tasks passa por g_state (protegido por mutex).
// O barramento SPI (TFT + rádio) é protegido por g_spiMutex. Todas as
// esperas usam vTaskDelay — nada bloqueia o scheduler (sem Core Panic).
//
#include <Arduino.h>
#include "board.h"
#include "app_state.h"
#include "screen_ui.h"
#include "gps_task.h"
#include "lorawan_task.h"
#include "battery.h"

void setup() {
  Serial.begin(115200);

  // 1) Bring-up OBRIGATÓRIO do T-Deck: energia, chip-selects altos, MISO
  //    pull-up e SPI.begin(). Precisa vir ANTES de tft.init()/radio.begin().
  boardInit();

  // 2) Mutex do estado compartilhado antes de qualquer Task tocá-lo.
  appStateInit();

  // 3) Inicializa a tela e desenha o splash (o bus já está pronto).
  screenInit();

  // 4) Inicializa a UART do GPS.
  gpsInit();

  // 4b) Inicializa o ADC de bateria.
  batteryInit();

  // 5) Inicializa o rádio LoRa (usa o bus já aberto pelo boardInit).
  int16_t radioState = loraInit();
  bool radioOk = (radioState == RADIOLIB_ERR_NONE);
  if (!radioOk) {
    Serial.printf("[MAIN] Radio nao inicializou (cod %d)\n", radioState);
    AppStateGuard guard;
    g_state.phase         = LoRaWANPhase::RADIO_ERROR;
    g_state.lastRadioCode = radioState;
    g_state.dirty         = true;
  }

  // 6) Sobe as Tasks. Core 0 para UI+GPS (leves), core 1 para o rádio
  //    (cujas janelas RX bloqueantes ficam isoladas).
  xTaskCreatePinnedToCore(uiTask,  "ui",  4096, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(gpsTask, "gps", 4096, nullptr, 1, nullptr, 0);

  if (radioOk) {
    // Stack maior: a pilha LoRaWAN + criptografia consome bastante.
    xTaskCreatePinnedToCore(loraTask, "lora", 8192, nullptr, 1, nullptr, 1);
  }
}

void loop() {
  // Nada aqui: todo o trabalho vive nas Tasks. Cedemos a CPU.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
