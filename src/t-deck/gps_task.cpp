#include "gps_task.h"
#include "app_state.h"
#include <TinyGPSPlus.h>

// Parser e porta serial privados deste módulo.
static TinyGPSPlus gps;
static HardwareSerial gpsSerial(1);   // UART1

void gpsInit() {
  // SERIAL_8N1, RX=44, TX=43 (mapeamento LilyGO). No ESP32-S3 a UART pode
  // mapear qualquer GPIO.
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
}

void gpsTask(void* pvParameters) {
  (void)pvParameters;

  uint32_t diagTick = 0;   // heartbeat de diagnóstico (~3s)

  for (;;) {
    // Drena tudo que chegou no buffer da UART e joga no parser.
    while (gpsSerial.available() > 0) {
      gps.encode(gpsSerial.read());
    }

    // Diagnóstico a cada ~3s (15 x 200ms):
    //   chars=0            -> nada chegando (pinos RX/TX trocados ou errados)
    //   chars sobe, cs>0   -> baud errado / ruído (sentencas corrompidas)
    //   chars sobe, cs=0   -> dados OK, só falta travar satélites (ceu aberto)
    if (diagTick == 0) {
      Serial.printf("[GPS] chars=%lu  sentences=%lu  checksumErr=%lu  fix=%d  sats=%lu\n",
                    (unsigned long)gps.charsProcessed(),
                    (unsigned long)gps.sentencesWithFix(),
                    (unsigned long)gps.failedChecksum(),
                    gps.location.isValid() ? 1 : 0,
                    (unsigned long)(gps.satellites.isValid() ? gps.satellites.value() : 0));
    }
    diagTick = (diagTick + 1) % 15;

    // Monta um snapshot local e só então trava o mutex para publicar.
    bool     fix   = gps.location.isValid();
    double   lat   = gps.location.lat();
    double   lon   = gps.location.lng();
    double   alt   = gps.altitude.meters();
    uint32_t sats  = gps.satellites.isValid() ? gps.satellites.value() : 0;
    uint32_t chars = gps.charsProcessed();

    {
      AppStateGuard guard;

      // Marca dirty só se algo relevante mudou, para a UI não repintar à toa.
      bool changed =
          (g_state.gpsFix       != fix)  ||
          (g_state.satellites   != sats) ||
          (fix && (g_state.latitude  != lat || g_state.longitude != lon));

      g_state.gpsFix       = fix;
      g_state.latitude     = lat;
      g_state.longitude    = lon;
      g_state.altitude     = alt;
      g_state.satellites   = sats;
      g_state.gpsCharsSeen = chars;

      if (changed) {
        g_state.dirty = true;
      }
    }

    // ~5 Hz: o ATGM336H emite NMEA a 1 Hz, não precisa varrer mais rápido.
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
