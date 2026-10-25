#include "battery.h"

void batteryInit() {
  // 12 bits (0..4095) e atenuação máxima para medir até ~2.5V no pino
  // (o divisor já reduz a tensão da bateria pela metade).
  analogReadResolution(12);
  analogSetPinAttenuation(BOARD_BAT_ADC_PIN, ADC_11db);
}

// Fator de calibração do ADC (ponto único) contra multímetro de referência.
//   leitura crua ~= 4.005 V  ->  real medido = 4.099 V
//   BATT_CAL = 4.099 / 4.005 = 1.0235
// Corrige o desvio do ADC do ESP32-S3 + tolerancia do divisor.
// Para recalibrar com o firmware ATUAL: multiplique o BATT_CAL atual pela
// razao (tensao_multimetro / tensao_mostrada_no_display).
#define BATT_CAL   1.0235f

float batteryReadVolts() {
  // Média de algumas amostras para reduzir ruído do ADC.
  const int N = 16;
  uint32_t acc = 0;
  for (int i = 0; i < N; i++) {
    acc += analogReadMilliVolts(BOARD_BAT_ADC_PIN);
  }
  float mv = (float)acc / N;                  // milivolts no pino
  // Divisor 1:2 (x2) + fator de calibração.
  return (mv * 2.0f * BATT_CAL) / 1000.0f;    // Volts
}

uint8_t batteryPercent(float volts) {
  // Curva linear para Li-Ion. vMax = 4.15V porque uma célula "cheia" em
  // repouso costuma ficar em ~4.1-4.15V (não chega limpo a 4.20V), então
  // uma carga completa marca ~100%. vMin = 3.30V ~= vazio.
  const float vMin = 3.30f;
  const float vMax = 4.15f;
  if (volts <= vMin) return 0;
  if (volts >= vMax) return 100;
  return (uint8_t)(((volts - vMin) / (vMax - vMin)) * 100.0f + 0.5f);
}

bool batteryIsCharging(float volts) {
  // Acima do máximo físico de uma célula Li-Ion -> lendo VBUS/carga.
  return volts > BATT_CHARGING_THRESHOLD;
}
