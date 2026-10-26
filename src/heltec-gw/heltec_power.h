#pragma once
//
// heltec_power.h
// --------------
// Gestão de energia do Heltec: botão PRG (GPIO0), leitura de bateria e
// deep sleep. Mantém a lógica de energia fora do main para ficar legível.
//
#include <Arduino.h>

// Evento do botão detectado neste ciclo.
enum class BtnEvent : uint8_t {
  NONE = 0,
  SHORT,   // clique curto  -> liga/desliga o OLED
  LONG     // pressão longa -> deep sleep (ultra econômico)
};

// Inicializa botão + ADC de bateria. Chame uma vez no setup().
void powerInit();

// Deve ser chamado com frequência (no loop). Retorna o evento do botão
// (NONE na maioria das vezes). Não bloqueia.
BtnEvent powerPollButton();

// Lê a tensão da bateria em Volts (habilita o ADC_Ctrl, amostra, desabilita).
float powerReadBattery();

// Diagnóstico: retorna o valor BRUTO lido no pino (mV, antes do fator do
// divisor). Útil para calibrar e descobrir se o pino/controle estão certos.
float powerReadBatteryRawMv();

// Entra em DEEP SLEEP. O rádio e o OLED devem ser desligados antes de chamar.
// Configura o GPIO0 como fonte de wake (nível baixo = botão pressionado):
// ao pressionar o botão novamente, a placa REINICIA (boot limpo) e volta a
// operar. Esta função não retorna.
void powerDeepSleep();
