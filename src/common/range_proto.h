#pragma once
//
// range_proto.h
// -------------
// Protocolo do range test LoRa bidirecional entre o T-Deck (móvel) e o
// Heltec (fixo, no ponto alto). Texto simples (ASCII) para ser fácil de
// depurar no serial e tolerante a parsing. Compartilhado pelos dois lados.
//
// Fluxo (half-duplex, no mesmo canal/SF de lora_test_params.h):
//   1. T-Deck transmite um PING com um contador (seq).
//   2. Heltec recebe, mede RSSI/SNR do PING, e responde um ACK com:
//        - o mesmo seq (para casar ida/volta)
//        - o RSSI e SNR que ELE mediu (qualidade do enlace de IDA)
//   3. T-Deck recebe o ACK, mede RSSI/SNR do ACK (qualidade de VOLTA),
//      e extrai os valores de ida que vieram no texto.
//
// Formatos:
//   PING:  "PING,<seq>"
//   ACK:   "ACK,<seq>,<rssi_ida>,<snr_ida>"
//     - rssi_ida: inteiro em dBm (ex: -42)
//     - snr_ida:  float em dB, 1 casa (ex: 9.5)
//
#include <Arduino.h>

#define RANGE_PING_PREFIX   "PING,"
#define RANGE_ACK_PREFIX    "ACK,"

// Monta o payload do PING. buf deve ter >= 24 bytes.
static inline void rangeMakePing(char* buf, size_t bufLen, uint32_t seq) {
  snprintf(buf, bufLen, RANGE_PING_PREFIX "%lu", (unsigned long)seq);
}

// Monta o payload do ACK. buf deve ter >= 48 bytes.
static inline void rangeMakeAck(char* buf, size_t bufLen, uint32_t seq,
                                int rssiFwd, float snrFwd) {
  snprintf(buf, bufLen, RANGE_ACK_PREFIX "%lu,%d,%.1f",
           (unsigned long)seq, rssiFwd, (double)snrFwd);
}

// Tenta interpretar um payload como PING. Retorna true e preenche seq.
static inline bool rangeParsePing(const char* msg, uint32_t* seq) {
  size_t pfx = strlen(RANGE_PING_PREFIX);
  if (strncmp(msg, RANGE_PING_PREFIX, pfx) != 0) return false;
  return sscanf(msg + pfx, "%lu", (unsigned long*)seq) == 1;
}

// Tenta interpretar um payload como ACK. Retorna true e preenche os campos.
static inline bool rangeParseAck(const char* msg, uint32_t* seq,
                                 int* rssiFwd, float* snrFwd) {
  size_t pfx = strlen(RANGE_ACK_PREFIX);
  if (strncmp(msg, RANGE_ACK_PREFIX, pfx) != 0) return false;
  return sscanf(msg + pfx, "%lu,%d,%f",
                (unsigned long*)seq, rssiFwd, snrFwd) == 3;
}
