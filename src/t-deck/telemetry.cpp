#include "telemetry.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, THINGSPEAK_WRITE_KEY

// Intervalo mínimo entre posts (plano gratuito do ThingSpeak ~15s).
static const uint32_t TS_MIN_INTERVAL_MS = 16000;
static uint32_t lastPostMs = 0;

// Status para a UI.
static uint32_t     postOkCount = 0;
static TsPostStatus lastStatus  = TsPostStatus::NONE;

uint32_t     telemetryPostCount()  { return postOkCount; }
TsPostStatus telemetryLastStatus() { return lastStatus; }

bool telemetryBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("[WiFi] Conectando a \"%s\"...\n", WIFI_SSID);
  uint32_t until = millis() + 15000;   // tenta por ~15s
  while (WiFi.status() != WL_CONNECTED && millis() < until) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("[WiFi] Falhou (segue sem telemetria).");
  return false;
}

bool telemetryWifiUp() {
  return WiFi.status() == WL_CONNECTED;
}

void telemetryPost(int rssiFwd, float snrFwd, int rssiRev, float snrRev,
                   uint32_t seq, uint32_t acks,
                   bool gpsFix, double lat, double lon) {
  // Respeita o intervalo mínimo do plano gratuito.
  uint32_t now = millis();
  if (lastPostMs != 0 && (now - lastPostMs) < TS_MIN_INTERVAL_MS) return;

  if (WiFi.status() != WL_CONNECTED) {
    // Tenta reconectar de forma não-bloqueante (sem travar o range test).
    WiFi.reconnect();
    return;
  }

  // Monta a URL do ThingSpeak (HTTP simples, sem HTTPS para ser leve).
  String url = "http://api.thingspeak.com/update?api_key=";
  url += THINGSPEAK_WRITE_KEY;
  url += "&field1=" + String(rssiFwd);
  url += "&field2=" + String(snrFwd, 1);
  url += "&field3=" + String(rssiRev);
  url += "&field4=" + String(snrRev, 1);
  url += "&field5=" + String(seq);
  url += "&field6=" + String(acks);
  if (gpsFix) {
    url += "&latitude="  + String(lat, 6);
    url += "&longitude=" + String(lon, 6);
  }

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String resp = http.getString();  // ThingSpeak responde o entry ID (>0 OK)
    Serial.printf("[TS] POST ok (HTTP %d, entry=%s)\n", code, resp.c_str());
    postOkCount++;
    lastStatus = TsPostStatus::OK;
  } else {
    Serial.printf("[TS] POST falhou (HTTP %d, %s)\n", code,
                  http.errorToString(code).c_str());
    lastStatus = TsPostStatus::FAIL;
  }
  http.end();

  lastPostMs = now;
}

bool telemetryPostWaypoint(uint8_t label, uint32_t wpCount,
                           int rssiFwd, float snrFwd,
                           bool gpsFix, double lat, double lon) {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    return false;
  }

  // field7 = label da marca; field8 = contador de marcas. Inclui RSSI/SNR de
  // ida e a posição, para o ponto marcado aparecer no mapa do ThingSpeak.
  String url = "http://api.thingspeak.com/update?api_key=";
  url += THINGSPEAK_WRITE_KEY;
  url += "&field1=" + String(rssiFwd);
  url += "&field2=" + String(snrFwd, 1);
  url += "&field7=" + String(label);
  url += "&field8=" + String(wpCount);
  if (gpsFix) {
    url += "&latitude="  + String(lat, 6);
    url += "&longitude=" + String(lon, 6);
  }

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  bool ok = (code == 200);
  Serial.printf("[TS] WAYPOINT label=%u wp=%lu -> HTTP %d\n",
                label, (unsigned long)wpCount, code);
  if (ok) { postOkCount++; lastStatus = TsPostStatus::OK; }
  else    { lastStatus = TsPostStatus::FAIL; }
  http.end();

  // Marca também conta para o espaçamento normal, para não estourar o limite.
  lastPostMs = millis();
  return ok;
}
