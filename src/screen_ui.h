#pragma once
//
// screen_ui.h
// -----------
// Tela personalizada do T-Deck. Este módulo é o ÚNICO dono do objeto
// TFT_eSPI. Nenhuma outra Task desenha no display diretamente — todas
// publicam dados em g_state e a UI Task lê e redesenha.
//
#include <Arduino.h>

// Inicializa o display (init + rotação + splash). Chame no setup(),
// na mesma Task que vai rodar a UI, depois de ligar o BOARD_POWERON.
void screenInit();

// Task da UI: redesenha a tela quando g_state.dirty == true.
// Parâmetro padrão do FreeRTOS (não usado).
void uiTask(void* pvParameters);

// Expõe a instância SPIClass (HSPI) que o TFT_eSPI inicializou. O rádio
// reaproveita o MESMO barramento para não haver disputa de pinos no T-Deck.
// Válido só após screenInit().
class SPIClass;
SPIClass& screenGetSPI();
