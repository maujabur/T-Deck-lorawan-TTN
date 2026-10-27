#include "telemetry.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, THINGSPEAK_WRITE_KEY, THINGSPEAK_CHANNEL_ID

// Controle do intervalo mínimo entre bulks (plano gratuito: >= 15s).
static uint32_t lastPostMs = 0;

// Status para a UI.
static uint32_t     postOkCount = 0;   // total de MEDIÇÕES gravadas (não de POSTs)
static TsPostStatus lastStatus  = TsPostStatus::NONE;

// Controle da reconexão ativa do WiFi.
static uint32_t lastWifiTryMs = 0;

uint32_t     telemetryPostCount()  { return postOkCount; }
TsPostStatus telemetryLastStatus() { return lastStatus; }

bool telemetryBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("[WiFi] Conectando a \"%s\"...\n", WIFI_SSID);
  uint32_t until = millis() + CFG_WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < until) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("[WiFi] Falhou (segue bufferizando).");
  return false;
}

bool telemetryWifiUp() {
  return WiFi.status() == WL_CONNECTED;
}

void telemetryForceReconnect() {
  Serial.println("[WiFi] Reconexao forcada pelo usuario.");
  WiFi.disconnect(true);        // derruba e esquece a sessão atual
  delay(50);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiTryMs = millis();
}

void telemetryEnsureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;   // tudo certo

  // Caiu: a cada CFG_WIFI_RETRY_MS refaz um WiFi.begin() de verdade (mais
  // robusto que o reconnect() passivo, que não voltava após o AP sumir).
  uint32_t now = millis();
  if (now - lastWifiTryMs >= CFG_WIFI_RETRY_MS) {
    lastWifiTryMs = now;
    Serial.println("[WiFi] Sem conexao, tentando reconectar...");
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

bool telemetryCanFlush() {
  return (lastPostMs == 0) || (millis() - lastPostMs >= CFG_FLUSH_INTERVAL_MS);
}

// Converte epoch UTC -> "YYYY-MM-DD HH:MM:SS +0000" (ISO aceito pelo TS).
static String isoUtc(time_t ts) {
  struct tm t;
  gmtime_r(&ts, &t);
  char b[32];
  snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d +0000",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
           t.tm_hour, t.tm_min, t.tm_sec);
  return String(b);
}

uint32_t telemetryBulkFlush(const Measurement* items, uint32_t n) {
  if (n == 0 || items == nullptr) return 0;
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return 0; }
  if (!telemetryCanFlush()) return 0;          // respeita janela de 15s
  if (n > CFG_BULK_MAX_POINTS) n = CFG_BULK_MAX_POINTS;

  // Monta o corpo JSON do bulk-update manualmente (sem libs externas).
  // { "write_api_key":"...", "updates":[ {created_at,field1..8,latitude,longitude}, ... ] }
  String body;
  body.reserve(n * 160 + 64);
  body += "{\"write_api_key\":\"";
  body += THINGSPEAK_WRITE_KEY;
  body += "\",\"updates\":[";

  for (uint32_t i = 0; i < n; i++) {
    const Measurement& m = items[i];
    if (i) body += ",";
    body += "{\"created_at\":\"";
    body += isoUtc(m.ts);
    // RSSI/SNR só têm significado COM ACK. Sem ACK, OMITIMOS field1..4: assim
    // field1 (rssi_ida) fica VAZIO no canal e serve de indicador de "sem sinal"
    // para a web — sem gastar um campo dedicado.
    if (m.gotAck) {
      body += "\",\"field1\":" + String(m.rssiFwd);
      body += ",\"field2\":"  + String(m.snrFwd, 1);
      body += ",\"field3\":"  + String(m.rssiRev);
      body += ",\"field4\":"  + String(m.snrRev, 1);
      body += ",\"field5\":"  + String(m.seq);
    } else {
      body += "\",\"field5\":" + String(m.seq);
    }
    body += ",\"field6\":"  + String(m.acks);
    if (m.wpLabel >= 0) body += ",\"field7\":" + String(m.wpLabel);
    body += ",\"latitude\":"  + String(m.lat, 6);
    body += ",\"longitude\":" + String(m.lon, 6);
    body += "}";
  }
  body += "]}";

  String url = "http://api.thingspeak.com/channels/";
  url += String((unsigned long)THINGSPEAK_CHANNEL_ID);
  url += "/bulk_update.json";

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  String resp = (code > 0) ? http.getString() : String();
  http.end();

  lastPostMs = millis();

  // Sucesso: resposta contém "success":true.
  if (code == 202 || code == 200 || resp.indexOf("\"success\":true") >= 0) {
    postOkCount += n;
    lastStatus = TsPostStatus::OK;
    Serial.printf("[TS] BULK ok: %lu medicoes (HTTP %d)\n", (unsigned long)n, code);
    return n;
  }

  lastStatus = TsPostStatus::FAIL;
  Serial.printf("[TS] BULK falhou (HTTP %d): %s\n", code, resp.c_str());
  return 0;
}
