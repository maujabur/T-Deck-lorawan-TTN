#include "heltec_power.h"
#include "heltec_config.h"
#include "heltec_board.h"      // HELTEC_VEXT (desligar periféricos no sleep)
#include <esp_sleep.h>
#include <RadioLib.h>

// ---- Botão (GPIO0, PRG): pressionado = LOW (pull-up) ----
static bool     btnWasDown   = false;
static uint32_t btnDownMs     = 0;
static bool     longFired     = false;  // evita disparar LONG repetido

void powerInit() {
  pinMode(HCFG_BUTTON_PIN, INPUT_PULLUP);

  // ADC de bateria: controle começa DESativado (economiza).
  pinMode(HCFG_VBAT_CTRL_PIN, OUTPUT);
  digitalWrite(HCFG_VBAT_CTRL_PIN, HCFG_VBAT_CTRL_ACTIVE == LOW ? HIGH : LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(HCFG_VBAT_ADC_PIN, ADC_11db);
}

BtnEvent powerPollButton() {
  bool down = (digitalRead(HCFG_BUTTON_PIN) == LOW);
  uint32_t now = millis();

  if (down && !btnWasDown) {
    // Começou a pressionar.
    btnWasDown = true;
    btnDownMs  = now;
    longFired  = false;
    return BtnEvent::NONE;
  }

  if (down && btnWasDown) {
    // Segurando: dispara LONG assim que cruza o limiar (feedback imediato).
    if (!longFired && (now - btnDownMs) >= HCFG_LONGPRESS_MS) {
      longFired = true;
      return BtnEvent::LONG;
    }
    return BtnEvent::NONE;
  }

  if (!down && btnWasDown) {
    // Soltou.
    btnWasDown = false;
    uint32_t held = now - btnDownMs;
    if (longFired) return BtnEvent::NONE;          // LONG já tratado
    if (held >= HCFG_BUTTON_DEBOUNCE_MS) return BtnEvent::SHORT;
  }
  return BtnEvent::NONE;
}

float powerReadBatteryRawMv() {
  // Habilita o divisor/ADC, espera estabilizar, amostra, desabilita.
  digitalWrite(HCFG_VBAT_CTRL_PIN, HCFG_VBAT_CTRL_ACTIVE);
  delay(10);

  uint32_t acc = 0;
  for (int i = 0; i < HCFG_VBAT_SAMPLES; i++) {
    acc += analogReadMilliVolts(HCFG_VBAT_ADC_PIN);
  }
  float mv = (float)acc / HCFG_VBAT_SAMPLES;   // mV no pino

  // Desabilita de volta para economizar.
  digitalWrite(HCFG_VBAT_CTRL_PIN, HCFG_VBAT_CTRL_ACTIVE == LOW ? HIGH : LOW);
  return mv;
}

float powerReadBattery() {
  return (powerReadBatteryRawMv() * HCFG_VBAT_FACTOR) / 1000.0f;  // Volts
}

void powerDeepSleep() {
  // Desliga o trilho de periféricos (OLED etc) para o sleep ser "ultra
  // econômico". Vext ativo em LOW -> HIGH desliga.
  pinMode(HELTEC_VEXT, OUTPUT);
  digitalWrite(HELTEC_VEXT, HIGH);

  // Espera o usuário SOLTAR o botão, senão acordaria imediatamente (o botão
  // já está em LOW). Pequeno debounce no fim.
  while (digitalRead(HCFG_BUTTON_PIN) == LOW) {
    delay(10);
  }
  delay(100);

  // Acorda quando o GPIO0 for a nível BAIXO (botão pressionado de novo).
  // No ESP32-S3 usamos ext1 (ext0 não cobre todos os casos no S3).
  esp_sleep_enable_ext1_wakeup(1ULL << HCFG_BUTTON_PIN,
                               ESP_EXT1_WAKEUP_ANY_LOW);

  Serial.println("[PWR] Deep sleep. Pressione o botao para ligar.");
  Serial.flush();
  esp_deep_sleep_start();   // não retorna; o wake reinicia a placa
}
