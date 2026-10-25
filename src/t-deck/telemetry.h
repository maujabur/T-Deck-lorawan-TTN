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

// Conecta no WiFi (não bloqueia para sempre: tenta por ~15s). Retorna true
// se conectou. Chame no setup() do modo range test.
bool telemetryBegin();

// Posta uma amostra no ThingSpeak. Campos:
//   field1=rssi_ida field2=snr_ida field3=rssi_volta field4=snr_volta
//   field5=seq      field6=acks    + localização (lat/lon) se houver fix.
// Não-bloqueante além do próprio HTTP. Respeita intervalo mínimo do plano
// gratuito (~15s) internamente; chamadas mais frequentes são ignoradas.
void telemetryPost(int rssiFwd, float snrFwd, int rssiRev, float snrRev,
                   uint32_t seq, uint32_t acks,
                   bool gpsFix, double lat, double lon);

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
