#include "screen_ui.h"
#include "app_state.h"
#include "board.h"
#include "battery.h"
#include <TFT_eSPI.h>

// A tela é privada deste módulo. Só a uiTask mexe nela => sem corrida.
static TFT_eSPI tft = TFT_eSPI();

// Instância SPI (HSPI) do display, reaproveitada pelo rádio.
SPIClass& screenGetSPI() {
  return tft.getSPIinstance();
}

// --- Helpers de cor por fase do scanner ---
static uint16_t phaseColor(LoRaWANPhase p) {
  switch (p) {
    case LoRaWANPhase::JOINED:      return TFT_GREEN;
    case LoRaWANPhase::SCANNING:    return TFT_YELLOW;
    case LoRaWANPhase::WAITING:     return TFT_ORANGE;
    case LoRaWANPhase::RADIO_ERROR: return TFT_RED;
    case LoRaWANPhase::BOOT:
    default:                        return TFT_CYAN;
  }
}

static const char* phaseText(LoRaWANPhase p) {
  switch (p) {
    case LoRaWANPhase::JOINED:      return "CONECTADO NA TTN!";
    case LoRaWANPhase::SCANNING:    return "Transmitindo Join...";
    case LoRaWANPhase::WAITING:     return "Sem resposta. Rotacionando...";
    case LoRaWANPhase::RADIO_ERROR: return "ERRO: Radio nao achado!";
    case LoRaWANPhase::BOOT:
    default:                        return "Iniciando hardware...";
  }
}

void screenInit() {
  // O bus SPI já foi aberto por boardInit(). Travamos durante a init do
  // display e garantimos que rádio/SD estão desselecionados.
  SpiBusGuard busGuard;
  digitalWrite(BOARD_RADIO_CS,  HIGH);
  digitalWrite(BOARD_SDCARD_CS, HIGH);

  tft.init();
  tft.setRotation(1);          // paisagem padrão do T-Deck (320x240)
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.println("=== T-DECK SCANNER ===");
}

// Faz uma cópia local do estado sob trava, depois desenha sem segurar o
// mutex (desenhar é lento; não bloqueamos as outras Tasks por isso).
#ifdef TEST_LORA_RANGE
// Tela dedicada do range test: números grandes e legíveis para olhar de
// relance andando. Mostra IDA (medido pelo Heltec) e VOLTA (medido aqui),
// taxa de ACK e há quanto tempo foi o último ACK.
static void drawRangeTest(const AppState& s) {
  tft.fillScreen(TFT_BLACK);

  // Cabeçalho
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(10, 6);
  tft.println("== RANGE TEST ==");

  // Estado do último ping: verde se teve ACK, vermelho se não.
  uint32_t ageMs = (s.rtLastAckMs > 0) ? (millis() - s.rtLastAckMs) : 0;
  bool linkUp = s.rtGotAck && (s.rtLastAckMs > 0) && (ageMs < 10000);

  tft.setTextSize(2);
  tft.setTextColor(linkUp ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.setCursor(10, 34);
  tft.print(linkUp ? "ENLACE OK" : "SEM ACK");

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 58);
  tft.printf("Ping #%lu   ACKs: %lu",
             (unsigned long)s.rtSeq, (unsigned long)s.rtAcks);

  tft.setCursor(10, 72);
  if (s.rtLastAckMs > 0) {
    tft.printf("Ultimo ACK ha %lu.%lus",
               (unsigned long)(ageMs / 1000), (unsigned long)((ageMs % 1000) / 100));
  } else {
    tft.print("Aguardando 1o ACK...");
  }

  // IDA / VOLTA
  tft.drawFastHLine(0, 88, tft.width(), TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(10, 96);
  tft.printf("IDA  R%d S%.1f", s.rtRssiFwd, s.rtSnrFwd);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 118);
  tft.printf("VOLTA R%d S%.1f", s.rtRssiRev, s.rtSnrRev);

  // GPS (coords para marcar no mapa de alcance)
  tft.drawFastHLine(0, 142, tft.width(), TFT_DARKGREY);
  tft.setTextSize(1);
  if (s.gpsFix) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(10, 150);
    tft.printf("GPS FIX  Sats:%u", s.satellites);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 164);
    tft.printf("Lat: %.6f", s.latitude);
    tft.setCursor(10, 178);
    tft.printf("Lon: %.6f", s.longitude);
    tft.setCursor(10, 192);
    tft.printf("Alt: %.1f m", s.altitude);
  } else {
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(10, 150);
    tft.printf("GPS sem fix (sats:%u)", s.satellites);
  }

  // Bateria no rodapé.
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(10, 212);
  if (s.battCharging) tft.printf("CHARGING %.2fV", s.battVolts);
  else                tft.printf("BAT %u%% %.2fV", s.battPercent, s.battVolts);
}
#endif // TEST_LORA_RANGE

static void drawFromSnapshot(const AppState& s) {
#ifdef TEST_LORA_RANGE
  drawRangeTest(s);
  return;
#endif
  tft.fillScreen(TFT_BLACK);

  // Cabeçalho
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(10, 8);
  tft.println("=== T-DECK SCANNER ===");

  // Bateria (canto superior direito).
  tft.setTextSize(1);
  char battBuf[24];
  uint16_t battCol;
  if (s.battCharging) {
    // Em USB/carga: tensão real da bateria não é observável aqui.
    battCol = TFT_CYAN;
    snprintf(battBuf, sizeof(battBuf), "CHARGING %.2fV", s.battVolts);
  } else {
    battCol = (s.battPercent > 50) ? TFT_GREEN
            : (s.battPercent > 20) ? TFT_YELLOW
                                   : TFT_RED;
    snprintf(battBuf, sizeof(battBuf), "BAT %u%% %.2fV", s.battPercent, s.battVolts);
  }
  tft.setTextColor(battCol, TFT_BLACK);
  // Alinha à direita com base na largura do texto.
  int16_t bw = tft.textWidth(battBuf);
  tft.setCursor(tft.width() - bw - 6, 28);
  tft.print(battBuf);

  // Bloco LoRaWAN
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 44);
  tft.print("LoRaWAN AU915");

  tft.setCursor(10, 66);
  tft.printf("Sub-banda: FSB %u", s.subBand);

  tft.setCursor(10, 88);
  tft.setTextColor(phaseColor(s.phase), TFT_BLACK);
  tft.print(phaseText(s.phase));

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(10, 112);
  tft.printf("Tentativas: %u   Ultimo cod: %d", s.joinAttempts, s.lastRadioCode);

  // Linha divisoria
  tft.drawFastHLine(0, 130, tft.width(), TFT_DARKGREY);

  // Bloco GPS
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(10, 140);
  tft.print("GPS ATGM336H");

  tft.setTextSize(1);
  if (s.gpsFix) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(10, 166);
    tft.printf("FIX OK  Sats: %u", s.satellites);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 182);
    tft.printf("Lat: %.6f", s.latitude);
    tft.setCursor(10, 196);
    tft.printf("Lon: %.6f", s.longitude);
    tft.setCursor(10, 210);
    tft.printf("Alt: %.1f m", s.altitude);
  } else {
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(10, 166);
    tft.printf("Procurando satelites...");
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(10, 182);
    tft.printf("Sats: %u   NMEA bytes: %u", s.satellites, s.gpsCharsSeen);
  }
}

void uiTask(void* pvParameters) {
  (void)pvParameters;

  uint32_t battTick = 0;   // contador para amostrar a bateria a cada ~5s

  for (;;) {
    // A cada ~5s (50 x 100ms) lê a bateria e publica no estado.
    if (battTick == 0) {
      float v  = batteryReadVolts();
      bool  charging = batteryIsCharging(v);
      uint8_t p = charging ? 100 : batteryPercent(v);
      Serial.printf("[BAT] %.2f V  (%u%%)%s\n", v, p, charging ? "  CHARGING" : "");
      AppStateGuard guard;
      // Só marca dirty se mudou de forma perceptível, para não repintar à toa.
      if ((uint8_t)g_state.battPercent != p ||
          g_state.battCharging != charging ||
          fabsf(g_state.battVolts - v) > 0.02f) {
        g_state.battVolts    = v;
        g_state.battPercent  = p;
        g_state.battCharging = charging;
        g_state.dirty        = true;
      } else {
        // Mantém o valor atualizado mesmo sem repintar.
        g_state.battVolts    = v;
        g_state.battPercent  = p;
        g_state.battCharging = charging;
      }
    }
    battTick = (battTick + 1) % 50;

#ifdef TEST_LORA_RANGE
    // No range test o contador "último ACK há Xs" precisa andar na tela
    // mesmo sem novos pacotes: força repaint ~1x/s (a cada 10 loops de 100ms).
    if ((battTick % 10) == 0) {
      AppStateGuard guard;
      g_state.dirty = true;
    }
#endif

    bool needRedraw = false;
    AppState snapshot;

    // Seção crítica curta: verifica dirty e copia o estado.
    {
      AppStateGuard guard;
      if (g_state.dirty) {
        snapshot      = g_state;   // cópia por valor
        g_state.dirty = false;
        needRedraw    = true;
      }
    }

    if (needRedraw) {
      // Desenha segurando o bus SPI compartilhado; desseleciona rádio/SD.
      SpiBusGuard busGuard;
      digitalWrite(BOARD_RADIO_CS,  HIGH);
      digitalWrite(BOARD_SDCARD_CS, HIGH);
      drawFromSnapshot(snapshot);
    }

    // ~10 Hz de taxa de atualização. Não precisa mais que isso para texto.
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
