#pragma once
//
// lora_test_params.h
// ------------------
// Parâmetros de rádio LoRa CRU (não-LoRaWAN) compartilhados entre o
// transmissor de teste (T-Deck, modo TEST_LORA_TX) e o receptor de bancada
// (Heltec gateway). Os dois lados PRECISAM usar exatamente estes valores,
// senão não se enxergam no ar. Fonte única da verdade -> inclua este header
// nos dois firmwares.
//
#define LORATEST_FREQ       915.0   // MHz
#define LORATEST_BW         125.0   // kHz
#define LORATEST_SF         9       // spreading factor
#define LORATEST_CR         7       // coding rate 4/7
#define LORATEST_SYNC       0x12    // sync word LoRa privado
#define LORATEST_PREAMBLE   8
#define LORATEST_TXPOWER    10      // dBm
