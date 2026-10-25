#pragma once
//
// keyboard.h
// ----------
// Teclado físico do T-Deck. É um ESP32-C3 separado que fala por I2C
// (endereço 0x55) no mesmo barramento I2C da placa (SDA=18, SCL=8).
// A leitura é igual à do firmware de fábrica da LilyGO: Wire.requestFrom(0x55,1)
// retorna o código ASCII da tecla pressionada (0 = nenhuma).
//
#include <Arduino.h>

// Pinos e endereço I2C do teclado no T-Deck (utilities.h LilyGO).
#define KB_I2C_SDA   18
#define KB_I2C_SCL   8
#define KB_I2C_ADDR  0x55

// ---- Trackball ("bolinha") ----
// NÃO vem pelo I2C do teclado: são GPIOs lidos direto pelo ESP32-S3.
// Cada direção é um pino que pulsa ao girar a bolinha; o clique é o BOOT.
// Fonte: utilities.h LilyGO (BOARD_TBOX_Gxx).
#define TB_UP      3    // BOARD_TBOX_G01
#define TB_DOWN    15   // BOARD_TBOX_G03
#define TB_LEFT    1    // BOARD_TBOX_G04
#define TB_RIGHT   2    // BOARD_TBOX_G02
#define TB_CLICK   0    // BOARD_BOOT_PIN (clique da bolinha)

// Direção lida do trackball.
enum class TrackballDir : uint8_t { NONE = 0, UP, DOWN, LEFT, RIGHT, CLICK };

// Inicializa o barramento I2C e detecta o teclado. Retorna true se o
// teclado respondeu. Chame uma vez no setup().
bool keyboardInit();

// Lê uma tecla (não-bloqueante). Retorna o código ASCII da tecla recém
// pressionada, ou 0 se nenhuma. Cada tecla é reportada uma única vez.
char keyboardRead();

// true se o teclado foi detectado no init.
bool keyboardPresent();

// Configura os GPIOs do trackball. Chame uma vez no setup() (depois de
// keyboardInit() ou junto). Usa pull-up interno; a borda indica movimento.
void trackballInit();

// Lê o trackball (não-bloqueante). Retorna a direção detectada desde a última
// chamada, ou NONE. CLICK tem prioridade.
TrackballDir trackballRead();
