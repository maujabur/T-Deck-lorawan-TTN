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

// Duração dos flashes de feedback na tela (SCREEN ON/OFF, WAKE, SLEEP), em ms.
// Valor único para todos, por simetria.
#define HCFG_FLASH_MS              500

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

// ============================================================
// PROTEÇÃO DE BATERIA (descarga profunda)
// ============================================================
// Abaixo de HCFG_VBAT_SLEEP_V (Volts), e por HCFG_VBAT_LOW_COUNT leituras
// seguidas (cada ~5s), o Heltec entra em deep sleep para proteger a celula
// Li-Ion. A contagem evita dormir num pico de queda durante um TX.
//
// Ao ACORDAR (o deep sleep reinicia a placa), o setup() so liga de verdade se
// a tensao estiver acima de HCFG_VBAT_WAKE_V (histerese, maior que a de
// dormir). Abaixo disso volta a dormir -> evita liga/desliga "zumbi" com a
// bateria no limite. Como nao da para detectar o USB diretamente no Heltec, a
// propria subida da tensao ao carregar e o que permite religar.
//
// 0 em HCFG_VBAT_SLEEP_V desativa a protecao.
#define HCFG_VBAT_SLEEP_V          3.20f   // dorme abaixo disto
#define HCFG_VBAT_WAKE_V           3.40f   // so religa acima disto (histerese)
#define HCFG_VBAT_LOW_COUNT        3       // leituras seguidas p/ confirmar
