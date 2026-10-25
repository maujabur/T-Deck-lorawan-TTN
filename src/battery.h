#pragma once
//
// battery.h
// ---------
// Monitor da bateria do LilyGO T-Deck.
// A tensão da bateria é lida no GPIO4 (BOARD_BAT_ADC) através de um divisor
// resistivo 1:2, ou seja, o ADC mede METADE da tensão real -> multiplicamos
// por 2 para obter os Volts da bateria.
//
#include <Arduino.h>

// Pino do ADC de bateria no T-Deck.
#define BOARD_BAT_ADC_PIN   4

// Inicializa o ADC. Chame uma vez no setup().
void batteryInit();

// Lê a tensão atual da bateria em Volts (já com o fator do divisor aplicado).
float batteryReadVolts();

// Converte Volts -> porcentagem aproximada para célula Li-Ion (3.3V..4.2V).
uint8_t batteryPercent(float volts);

// Detecta carga/USB: uma célula Li-Ion não passa de ~4.25V. Acima disso, o
// pino está lendo o trilho de VBUS/carga, logo está em USB/carregando.
#define BATT_CHARGING_THRESHOLD   4.35f
bool batteryIsCharging(float volts);
