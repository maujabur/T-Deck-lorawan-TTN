#include "telemetry.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, THINGSPEAK_WRITE_KEY

// Intervalo mínimo entre updates (plano gratuito do ThingSpeak ~15s).
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

// Faz um update no ThingSpeak e retorna o entry_id.
//   > 0  -> gravado com sucesso
//   0    -> ThingSpeak RECUSOU (quase sempre rate limit: update < ~15s).
//           ATENÇÃO: o HTTP ainda é 200 nesse caso! Só o body "0" denuncia.
//   < 0  -> erro de rede/HTTP
static long tsUpdate(const String& url) {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    return -1;
  }
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  long entry = -1;
  if (code == 200) {
    String body = http.getString();  // ThingSpeak responde o entry_id (ou "0")
    entry = body.toInt();
  }
  http.end();
  return entry;
}

void telemetryPost(int rssiFwd, float snrFwd, int rssiRev, float snrRev,
                   uint32_t seq, uint32_t acks,
                   bool gpsFix, double lat, double lon) {
  // Respeita o intervalo mínimo do plano gratuito.
  uint32_t now = millis();
  if (lastPostMs != 0 && (now - lastPostMs) < TS_MIN_INTERVAL_MS) return;

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

  long entry = tsUpdate(url);
  if (entry > 0) {
    postOkCount++;
    lastStatus = TsPostStatus::OK;
    lastPostMs = millis();
    Serial.printf("[TS] POST ok (entry=%ld)\n", entry);
  } else if (entry == 0) {
    // Recusado por rate limit: não atualiza lastPostMs para tentar de novo
    // no próximo ciclo (quando a janela já tiver passado).
    Serial.println("[TS] POST recusado (rate limit) - tentara de novo");
  } else {
    lastStatus = TsPostStatus::FAIL;
    Serial.println("[TS] POST falhou (rede)");
  }
}

bool telemetryPostWaypoint(uint8_t label, uint32_t wpCount,
                           int rssiFwd, float snrFwd,
                           bool gpsFix, double lat, double lon) {
  // field7 = label; field8 = contador. Inclui RSSI/SNR de ida e posição.
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

  // Uma MARCA é um evento importante: se o ThingSpeak recusar por rate limit
  // (entry==0), aguardamos a janela e tentamos de novo (até ~3 vezes).
  for (int attempt = 0; attempt < 3; attempt++) {
    // Garante o intervalo mínimo desde o último update bem-sucedido.
    uint32_t since = millis() - lastPostMs;
    if (lastPostMs != 0 && since < TS_MIN_INTERVAL_MS) {
      vTaskDelay(pdMS_TO_TICKS(TS_MIN_INTERVAL_MS - since));
    }

    long entry = tsUpdate(url);
    if (entry > 0) {
      postOkCount++;
      lastStatus = TsPostStatus::OK;
      lastPostMs = millis();
      Serial.printf("[TS] WAYPOINT label=%u wp=%lu -> entry=%ld\n",
                    label, (unsigned long)wpCount, entry);
      return true;
    } else if (entry == 0) {
      Serial.printf("[TS] WAYPOINT recusado (rate limit), tentativa %d\n", attempt + 1);
      // Força esperar uma janela inteira na próxima iteração.
      lastPostMs = millis();
    } else {
      lastStatus = TsPostStatus::FAIL;
      Serial.println("[TS] WAYPOINT falhou (rede)");
      return false;
    }
  }
  return false;
}
