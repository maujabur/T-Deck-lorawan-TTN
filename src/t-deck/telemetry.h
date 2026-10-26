#pragma once
//
// telemetry.h
// -----------
// Envio de telemetria do range test para o ThingSpeak via WiFi (hotspot).
// Só é usado quando TEST_LORA_RANGE + RANGE_TELEMETRY estão definidos.
//
// O T-Deck (que anda com você e com o hotspot) é quem posta — o Heltec fica
// autônomo só respondendo ACK. Isso evita o problema de o gateway perder o
// WiFi quando você se afasta.
//
#include <Arduino.h>

#include "measurement_buffer.h"   // struct Measurement

// Conecta no WiFi (não bloqueia para sempre: tenta por ~15s). Retorna true
// se conectou. Chame no setup() do modo range test.
bool telemetryBegin();

// Envia um LOTE de medições ao ThingSpeak via bulk-update (um único POST
// com várias linhas, cada uma com seu created_at vindo do GPS). É assim que
// aproveitamos TODAS as medições (online) e descarregamos o buffer (offline).
// Retorna o número de medições gravadas com sucesso (0 = falhou/recusado).
// Respeita o intervalo mínimo entre bulks internamente.
uint32_t telemetryBulkFlush(const Measurement* items, uint32_t n);

// true se já passou tempo suficiente desde o último bulk (>= intervalo free).
bool telemetryCanFlush();

// true se o WiFi está conectado no momento.
bool telemetryWifiUp();

// Status do último POST para a UI mostrar de forma compacta.
enum class TsPostStatus : uint8_t {
  NONE = 0,   // ainda não tentou postar
  OK,         // último POST retornou HTTP 200
  FAIL        // último POST falhou
};

// Contador de posts OK acumulados.
uint32_t telemetryPostCount();

// Status do último POST.
TsPostStatus telemetryLastStatus();
