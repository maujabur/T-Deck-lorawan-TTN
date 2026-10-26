#pragma once
//
// heltec_config.h
// ---------------
// Parâmetros CONFIGURÁVEIS do firmware do Heltec (energia, botão, bateria,
// display), agrupados num só lugar. Pinos de hardware fixos ficam em
// heltec_board.h; aqui ficam os de comportamento e calibração.
//

// ============================================================
// BOTÃO (PRG / GPIO0)
// ============================================================
#define HCFG_BUTTON_PIN            0        // GPIO0 (botão PRG do Heltec)

// Duração (ms) a partir da qual uma pressão é considerada "longa".
// Curto  (< isto)  -> liga/desliga o OLED.
// Longo  (>= isto) -> entra em deep sleep (ultra econômico).
#define HCFG_LONGPRESS_MS          1500

// Debounce do botão (ms).
#define HCFG_BUTTON_DEBOUNCE_MS    40

// ============================================================
// DISPLAY (orientação e economia)
// ============================================================
// Rotação do OLED (Adafruit_GFX): 0/2 = paisagem (128x64),
// 1/3 = RETRATO (64x128). Para o case do Heltec em pé, use 1 ou 3.
#define HCFG_DISPLAY_ROTATION      1

// Após este tempo sem atividade, o OLED dorme sozinho (volta no botão).
// 0 = desativa o auto-sleep do display.
#define HCFG_DISPLAY_TIMEOUT_MS    60000UL

// ============================================================
// BATERIA (leitura de tensão)
// ============================================================
// No Heltec V3 a tensão da bateria é lida no GPIO1, habilitada por um pino
// de controle (ADC_Ctrl = GPIO37). O nível que ATIVA a leitura varia entre
// revisões; se a tensão vier zerada/errada, inverta HCFG_VBAT_CTRL_ACTIVE.
#define HCFG_VBAT_ADC_PIN          1        // GPIO1 (VBAT_READ)
#define HCFG_VBAT_CTRL_PIN         37       // GPIO37 (ADC_Ctrl)
#define HCFG_VBAT_CTRL_ACTIVE      HIGH     // nível que habilita a leitura

// Fator do divisor resistivo. A tensão real da bateria = mV_no_pino * fator.
// Valor inicial aproximado (~4.9x); CALIBRE comparando com um multímetro:
//   HCFG_VBAT_FACTOR = tensao_multimetro / tensao_mostrada.
#define HCFG_VBAT_FACTOR           4.9f

// Nº de amostras do ADC por leitura (média, reduz ruído).
#define HCFG_VBAT_SAMPLES          16
