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
#include "config.h"    // CFG_VBAT_* (proteção de bateria no boot)
#ifdef TEST_KEYBOARD
#include "keyboard.h"
#include <TFT_eSPI.h>
#endif

void setup() {
  Serial.begin(115200);

  // 1) Bring-up OBRIGATÓRIO do T-Deck: energia, chip-selects altos, MISO
  //    pull-up e SPI.begin(). Precisa vir ANTES de tft.init()/radio.begin().
  boardInit();

#ifdef TEST_KEYBOARD
  // ===== MODO DE TESTE DO TECLADO =====
  // Isolado: inicializa só tela + teclado e fica mostrando as teclas lidas.
  // Não sobe GPS/rádio. Valida o hardware do teclado I2C (0x55) antes de
  // construir qualquer feature em cima dele.
  appStateInit();
  screenInit();
  bool kbOk = keyboardInit();
  trackballInit();
  Serial.printf("[KBTEST] teclado %s + trackball\n", kbOk ? "DETECTADO" : "NAO encontrado");
  screenDebugLines("TESTE TECLADO",
                   kbOk ? "Teclado: DETECTADO" : "Teclado: NAO ACHADO",
                   "Tecla ou bolinha...");

  // Loop de teste próprio (não retorna).
  uint32_t keyCount = 0;
  const char* tbName[] = { "NONE", "UP", "DOWN", "LEFT", "RIGHT", "CLICK" };
  for (;;) {
    char c = keyboardRead();
    if (c != 0) {
      keyCount++;
      Serial.printf("[KBTEST] tecla: 0x%02X ('%c')  total=%lu\n",
                    (uint8_t)c, (c >= 32 && c < 127) ? c : '.',
                    (unsigned long)keyCount);
      char l2[40], l3[40];
      snprintf(l2, sizeof(l2), "Tecla: 0x%02X '%c'",
               (uint8_t)c, (c >= 32 && c < 127) ? c : ' ');
      snprintf(l3, sizeof(l3), "Total de teclas: %lu", (unsigned long)keyCount);
      screenDebugLines("TESTE TECLADO", l2, l3);
    }

    TrackballDir d = trackballRead();
    if (d != TrackballDir::NONE) {
      Serial.printf("[KBTEST] trackball: %s\n", tbName[(uint8_t)d]);
      char l3[40];
      snprintf(l3, sizeof(l3), "Bolinha: %s", tbName[(uint8_t)d]);
      screenDebugLines("TESTE TECLADO", "Mexa a bolinha", l3);
      // Debounce simples do clique para não floodar.
      if (d == TrackballDir::CLICK) delay(200);
    }

    delay(10);
  }
#endif

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

  // 5b) Proteção ao LIGAR (histerese): se a bateria ainda está fraca (abaixo de
  //     CFG_VBAT_WAKE_V), volta a dormir em vez de ligar só para drenar de novo.
  //     Só vale com bateria plausível (> 0.1 V) e não carregando. Como dormimos
  //     sem fonte de wake, chegar aqui significa que a chave foi religada —
  //     então a histerese evita o ciclo "religa-dorme" com a bateria no limite.
  if (CFG_VBAT_SLEEP_V > 0.0f) {
    float v = batteryReadVolts();
    if (!batteryIsCharging(v) && v > 0.1f && v < CFG_VBAT_WAKE_V) {
      batteryDeepSleepProtect(v);   // não retorna
    }
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
