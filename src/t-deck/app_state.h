#pragma once
//
// app_state.h
// ------------
// Estado compartilhado entre as Tasks do T-Deck (GPS, LoRaWAN e UI).
//
// REGRA DE OURO (anti Core Panic):
//   Nenhuma Task acessa os campos abaixo sem antes travar o mutex
//   `appStateMutex`. Use SEMPRE os helpers appStateLock()/appStateUnlock()
//   ou o escopo AppStateGuard. Isso evita corrida de dados / corrupção de
//   heap quando duas Tasks em cores diferentes tocam a mesma struct.
//
#include <Arduino.h>

// --------- Estado do scanner LoRaWAN ---------
enum class LoRaWANPhase : uint8_t {
  BOOT,          // inicializando rádio
  SCANNING,      // transmitindo join-request na sub-banda atual
  WAITING,       // janela de espera de 20s antes da próxima tentativa
  JOINED,        // conectado com sucesso na TTN
  RADIO_ERROR    // rádio não inicializou
};

struct AppState {
  // ---- GPS (ATGM336H) ----
  bool     gpsFix        = false;   // true quando há posição válida
  double   latitude      = 0.0;
  double   longitude     = 0.0;
  double   altitude      = 0.0;
  uint32_t satellites    = 0;
  uint32_t gpsCharsSeen  = 0;       // diagnóstico: bytes NMEA recebidos

  // ---- Scanner LoRaWAN ----
  LoRaWANPhase phase     = LoRaWANPhase::BOOT;
  uint8_t  subBand       = 2;       // FSB atual (1 ou 2) na região AU915
  int16_t  lastRadioCode = 0;       // último código de retorno do RadioLib
  uint32_t joinAttempts  = 0;

  // ---- Bateria ----
  float    battVolts     = 0.0f;    // tensão da bateria em Volts
  uint8_t  battPercent   = 0;       // carga estimada 0-100%
  bool     battCharging  = false;   // true quando em USB/carga (V acima do max Li-Ion)

  // ---- Flag de "algo mudou" para a UI redesenhar só quando necessário ----
  volatile bool dirty    = true;
};

// Instância global única + mutex. Definidos em app_state.cpp
extern AppState     g_state;
extern SemaphoreHandle_t g_stateMutex;

// Cria o mutex. Chame uma vez no setup() ANTES de criar as Tasks.
void appStateInit();

// Helpers de trava. portMAX_DELAY => bloqueia até conseguir (seguro aqui
// porque as seções críticas são curtíssimas: apenas cópia de campos).
static inline void appStateLock()   { xSemaphoreTake(g_stateMutex, portMAX_DELAY); }
static inline void appStateUnlock() { xSemaphoreGive(g_stateMutex); }

// RAII: trava no construtor, destrava ao sair do escopo.
struct AppStateGuard {
  AppStateGuard()  { appStateLock(); }
  ~AppStateGuard() { appStateUnlock(); }
};
