#pragma once
//
// measurement_buffer.h
// --------------------
// Buffer circular de medições do range test, alocado na PSRAM (8 MB no
// T-Deck). Guarda cada medição com timestamp UTC (do GPS) para que, ao
// reconectar, possam ser enviadas em lote (bulk-update) ao ThingSpeak com
// o horário correto — reconstruindo a trilha no tempo certo.
//
// Circular: quando enche (CFG_BUFFER_CAPACITY), descarta a MAIS ANTIGA.
//
#include <Arduino.h>
#include <time.h>

// Uma medição do range test pronta para virar uma linha no ThingSpeak.
struct Measurement {
  time_t   ts;         // horário UTC (epoch) da medição, vindo do GPS
  int16_t  rssiFwd;    // field1
  float    snrFwd;     // field2
  int16_t  rssiRev;    // field3
  float    snrRev;     // field4
  uint32_t seq;        // field5
  uint32_t acks;       // field6
  int16_t  wpLabel;    // field7 (-1 = não é waypoint)
  double   lat;        // latitude
  double   lon;        // longitude
};

// Inicializa o buffer na PSRAM. Retorna true se conseguiu alocar.
// Chame uma vez no setup() do modo range.
bool bufferInit();

// Adiciona uma medição. Se cheio, descarta a mais antiga (circular).
void bufferPush(const Measurement& m);

// Quantidade atual de medições no buffer.
uint32_t bufferCount();

// Capacidade total.
uint32_t bufferCapacity();

// Copia até maxOut medições MAIS ANTIGAS para out[], SEM removê-las ainda
// (peek). Retorna quantas copiou. Use antes de enviar um lote; só remova
// com bufferPopFront() após o envio bem-sucedido.
uint32_t bufferPeek(Measurement* out, uint32_t maxOut);

// Remove as 'n' medições mais antigas (após envio confirmado).
void bufferPopFront(uint32_t n);

// Esvazia o buffer.
void bufferClear();
