#include "lorawan_task.h"
#include "app_state.h"
#include "board.h"
#include "screen_ui.h"
#include <SPI.h>
#include <math.h>

// Ambos os modos de teste usam LoRa cru com os mesmos parâmetros do Heltec.
#if defined(TEST_LORA_TX) || defined(TEST_LORA_RANGE)
#define LORA_RAW_TEST 1
#include "lora_test_params.h"   // parâmetros de rádio compartilhados c/ Heltec
#endif
#ifdef TEST_LORA_RANGE
#include "config.h"             // parâmetros configuráveis (intervalos, buffer)
#include "range_proto.h"        // protocolo PING/ACK
#include "measurement_buffer.h" // buffer circular na PSRAM
#endif
#ifdef RANGE_TELEMETRY
#include "telemetry.h"          // upload WiFi -> ThingSpeak (bulk)
#endif

// ---- Hardware do rádio (privado deste módulo) ----
// No T-Deck o display e o rádio compartilham os MESMOS pinos SPI (40/41/38).
// Para não haver disputa pela matriz de pinos do ESP32-S3, o rádio usa a
// MESMA instância SPIClass (HSPI) do display, criada tardiamente em
// loraInit() (depois de screenInit()). O acesso é serializado por g_spiMutex.
static Module* radioModule = nullptr;
static SX1262* radioPtr    = nullptr;
#define radio (*radioPtr)

// Na RadioLib 6.6.0 a sub-banda é fixada no CONSTRUTOR do LoRaWANNode e não
// existe método begin()/selectSubBand(). Para rotacionar FSB precisamos
// recriar o nó. Fazemos isso só a cada 20s (não a cada iteração do loop,
// como no código antigo que estourava a pilha) usando um ponteiro estático.
static LoRaWANNode* node = nullptr;

// (Re)cria o nó na sub-banda desejada. Seguro: ocorre no máximo a cada 20s.
static void rebuildNode(uint8_t subBand) {
  if (node != nullptr) {
    delete node;
    node = nullptr;
  }
  node = new LoRaWANNode(&radio, &AU915, subBand);
}

// ---- Credenciais TTN (preencha com as SUAS chaves) ----
static uint64_t joinEUI = 0x0000000000000000ULL;
static uint64_t devEUI  = 0x70B3D57ED0079A4CULL;
static uint8_t  appKey[] = {
  0x34, 0x06, 0x42, 0x8A, 0x92, 0x55, 0xCD, 0x5D,
  0x84, 0xD0, 0x15, 0xAE, 0xF7, 0x95, 0xAB, 0xD1
};
// Em LoRaWAN 1.0.x o nwkKey é o mesmo appKey. Mantemos separado por clareza.
static uint8_t* nwkKey = appKey;

// Publica a fase atual no estado compartilhado e marca a UI como suja.
static void publishPhase(LoRaWANPhase phase, uint8_t subBand,
                         int16_t code, uint32_t attempts) {
  AppStateGuard guard;
  g_state.phase         = phase;
  g_state.subBand       = subBand;
  g_state.lastRadioCode = code;
  g_state.joinAttempts  = attempts;
  g_state.dirty         = true;
}

int16_t loraInit() {
  // Cria o Module usando a MESMA instância SPI (HSPI) do display.
  // Precisa ser chamado DEPOIS de screenInit().
  radioModule = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY,
                           screenGetSPI());
  radioPtr = new SX1262(radioModule);

  // Inicializa o rádio sob a trava do bus, com os outros CS desselecionados.
  SpiBusGuard busGuard;
  digitalWrite(BOARD_TFT_CS,    HIGH);
  digitalWrite(BOARD_SDCARD_CS, HIGH);

#ifdef LORA_RAW_TEST
  // MODO DE TESTE: LoRa cru nos mesmos parâmetros do receptor Heltec.
  int16_t state = radio.begin(LORATEST_FREQ, LORATEST_BW, LORATEST_SF,
                              LORATEST_CR, LORATEST_SYNC, LORATEST_TXPOWER,
                              LORATEST_PREAMBLE);
#else
  int16_t state = radio.begin();
#endif
  return state;
}

void loraSleep() {
  if (radioPtr == nullptr) return;   // rádio não inicializou: nada a fazer
  SpiBusGuard busGuard;
  digitalWrite(BOARD_TFT_CS,    HIGH);
  digitalWrite(BOARD_SDCARD_CS, HIGH);
  radio.sleep();
}

void loraTask(void* pvParameters) {
  (void)pvParameters;

#ifdef TEST_LORA_RANGE
  // ===== MODO RANGE TEST BIDIRECIONAL =====
  // T-Deck transmite PING, troca para RX e espera o ACK do Heltec. O ACK traz
  // o RSSI/SNR de IDA (medido pelo Heltec); o RSSI/SNR de VOLTA é medido aqui.
  // Mostra tudo na tela para teste de alcance a pé, sem TTN nem internet.
  bufferInit();   // aloca o buffer circular na PSRAM
#ifdef RANGE_TELEMETRY
  // Opcional: conecta no WiFi. Se falhar, o range test continua e as
  // medições vão para o buffer até a internet voltar.
  telemetryBegin();
#endif

  uint32_t seq = 0;
  for (;;) {
    seq++;
    char ping[24];
    rangeMakePing(ping, sizeof(ping), seq);

    // Cadência de medição desta iteração: mais rápida online, mais lenta
    // offline. Calculada no topo para também derivar o limiar de "SEM ACK".
#ifdef RANGE_TELEMETRY
    uint32_t waitMs = telemetryWifiUp() ? CFG_MEAS_INTERVAL_ONLINE_MS
                                        : CFG_MEAS_INTERVAL_OFFLINE_MS;
#else
    uint32_t waitMs = CFG_MEAS_INTERVAL_ONLINE_MS;
#endif
    // A tela mostra "SEM ACK" quando o último ACK passa deste limiar. Usamos
    // ~2,25x a cadência: cobre um ping perdido com folga, sem acusar perda só
    // porque offline o loop é mais lento (16s > 10s fixos de antes).
    uint32_t staleMs = waitMs * 2 + waitMs / 4;

    bool  gotAck   = false;
    int   rssiFwd  = 0;      // IDA (vem no ACK)
    float snrFwd   = 0.0f;
    int   rssiRev  = 0;      // VOLTA (medido aqui no ACK)
    float snrRev   = 0.0f;
    int16_t rxSt   = 0;      // motivo da (não) recepção, para diagnóstico

    {
      SpiBusGuard busGuard;
      digitalWrite(BOARD_TFT_CS,    HIGH);
      digitalWrite(BOARD_SDCARD_CS, HIGH);

      int16_t txSt = radio.transmit(ping);          // 1) transmite o PING
      if (txSt == RADIOLIB_ERR_NONE) {
        String resp;
        rxSt = radio.receive(resp, CFG_ACK_TIMEOUT_MS);  // 2) ACK
        if (rxSt == RADIOLIB_ERR_NONE) {
          uint32_t ackSeq = 0;
          if (rangeParseAck(resp.c_str(), &ackSeq, &rssiFwd, &snrFwd) &&
              ackSeq == seq) {
            gotAck  = true;
            rssiRev = (int)lround(radio.getRSSI());
            snrRev  = radio.getSNR();
          }
        }
      }
    }

    // Diagnóstico do motivo quando NÃO houve ACK, para distinguir:
    //   TIMEOUT     -> silêncio (nada chegou: alcance/obstrução)
    //   CRC_MISMATCH-> chegou corrompido (INTERFERÊNCIA de RF, ex.: no carro)
    //   outro       -> seq trocado / ruído
    if (!gotAck) {
      const char* why = (rxSt == RADIOLIB_ERR_RX_TIMEOUT)   ? "timeout(silencio)"
                      : (rxSt == RADIOLIB_ERR_CRC_MISMATCH) ? "CRC(interferencia)"
                                                            : "outro";
      Serial.printf("[RANGE] seq=%lu SEM ACK (%s, rx=%d)\n",
                    (unsigned long)seq, why, rxSt);
    }

    // Lê estado de GPS/waypoint e publica a medição para a UI.
    bool     fix; double lat, lon; time_t epoch;
    uint32_t acks; bool wpPending; uint8_t wpLabel; uint32_t wpCount;
    {
      AppStateGuard guard;
      g_state.rtSeq    = seq;
      g_state.rtGotAck = gotAck;
      if (gotAck) {
        g_state.rtAcks++;
        g_state.rtRssiFwd = rssiFwd; g_state.rtSnrFwd = snrFwd;
        g_state.rtRssiRev = rssiRev; g_state.rtSnrRev = snrRev;
        g_state.rtLastAckMs = millis();
      }
      g_state.phase = gotAck ? LoRaWANPhase::JOINED : LoRaWANPhase::WAITING;
      g_state.rtStaleMs = staleMs;   // limiar de "SEM ACK" conforme a cadência
      g_state.dirty = true;

      fix   = g_state.gpsFix;  lat = g_state.latitude; lon = g_state.longitude;
      epoch = g_state.gpsEpoch; acks = g_state.rtAcks;
      wpPending = g_state.wpPending; wpLabel = g_state.wpLastLabel;
      wpCount = g_state.wpCount;
      g_state.wpPending = false;   // consome a marca pendente (vai no buffer)
    }

    Serial.printf("[RANGE] seq=%lu ack=%d IDA %d/%.1f VOLTA %d/%.1f%s\n",
                  (unsigned long)seq, gotAck ? 1 : 0, rssiFwd, snrFwd,
                  rssiRev, snrRev, wpPending ? "  [MARCA]" : "");

    // Bufferiza a medição SÓ se houver ACK + FIX de posição + hora GPS válida.
    // O GPS trava a HORA antes do fix de posição 3D; sem fix, lat/lon seria
    // (0,0) ("Null Island") e poluiria o mapa. Então exigimos fix de verdade.
    if (gotAck && fix && epoch > 0) {
      Measurement m;
      m.ts = epoch; m.rssiFwd = rssiFwd; m.snrFwd = snrFwd;
      m.rssiRev = rssiRev; m.snrRev = snrRev;
      m.seq = seq; m.acks = acks;
      m.wpLabel = wpPending ? (int16_t)wpLabel : (int16_t)-1;
      m.lat = lat; m.lon = lon;
      bufferPush(m);
    } else if (gotAck) {
      // Teve ACK mas NÃO entrou no buffer: diz por quê, para não parecer que a
      // telemetria está quebrada quando na verdade falta fix/hora de GPS.
      Serial.printf("[RANGE] nao bufferizado (sem %s) -> aguardando GPS\n",
                    !fix ? "fix de posicao" : "hora GPS");
    }

#ifdef RANGE_TELEMETRY
    // Mantém o WiFi vivo: se caiu, tenta reconectar ativamente (periódico).
    telemetryEnsureWifi();

    // Descarrega o buffer em bulk quando online e a janela permitir. Mesmo
    // caminho serve para o escoamento online (a cada 16s) e para esvaziar o
    // acúmulo offline quando a internet volta.
    if (telemetryWifiUp() && telemetryCanFlush() && bufferCount() > 0) {
      static Measurement batch[CFG_BULK_MAX_POINTS];
      uint32_t n = bufferPeek(batch, CFG_BULK_MAX_POINTS);
      uint32_t sent = telemetryBulkFlush(batch, n);
      if (sent > 0) bufferPopFront(sent);   // só remove o que foi gravado
    }

    // Status para a UI (inclui tamanho do buffer).
    {
      AppStateGuard guard;
      g_state.tsEnabled    = true;
      g_state.wifiUp       = telemetryWifiUp();
      g_state.tsLastStatus = (uint8_t)telemetryLastStatus();
      g_state.tsPostCount  = telemetryPostCount();
      g_state.bufCount     = bufferCount();
      g_state.dirty        = true;
    }
#endif

    // Aguarda até a próxima medição (cadência calculada no topo do loop).
    vTaskDelay(pdMS_TO_TICKS(waitMs));
  }
#endif

#ifdef TEST_LORA_TX
  // ===== MODO DE TESTE PONTO-A-PONTO =====
  // Transmite um "ping" periódico em LoRa cru para o receptor Heltec validar
  // o rádio do T-Deck. NÃO faz LoRaWAN. Reutiliza a UI para mostrar o status.
  uint32_t pingCount = 0;
  for (;;) {
    pingCount++;
    char msg[32];
    snprintf(msg, sizeof(msg), "T-DECK PING %lu", (unsigned long)pingCount);

    int16_t txState;
    {
      SpiBusGuard busGuard;
      digitalWrite(BOARD_TFT_CS,    HIGH);
      digitalWrite(BOARD_SDCARD_CS, HIGH);
      txState = radio.transmit(msg);
      // Volta a config de TX (transmit deixa o rádio em standby).
    }

    Serial.printf("[TEST-TX] enviado \"%s\" (cod %d)\n", msg, txState);

    // Mostra na tela via estado compartilhado (reusa campos existentes).
    {
      AppStateGuard guard;
      g_state.phase         = (txState == RADIOLIB_ERR_NONE)
                              ? LoRaWANPhase::SCANNING : LoRaWANPhase::RADIO_ERROR;
      g_state.lastRadioCode = txState;
      g_state.joinAttempts  = pingCount;
      g_state.dirty         = true;
    }

    // Transmite a cada ~3s. Não-bloqueante (cede CPU para UI/GPS).
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
#endif

  // Obs.: a checagem de rádio ausente é feita no main.cpp via loraInit();
  // se o rádio não subiu, esta Task nem chega a ser criada.
  uint8_t  subBand  = 2;   // começa pela FSB 2 (padrão Brasil/AU915)
  uint32_t attempts = 0;

  for (;;) {
    attempts++;
    publishPhase(LoRaWANPhase::SCANNING, subBand, 0, attempts);
    Serial.printf("[LoRaWAN] Join na FSB %u (tentativa %u)\n", subBand, attempts);

    // (Re)cria o nó na sub-banda atual e injeta as credenciais.
    rebuildNode(subBand);
    node->beginOTAA(joinEUI, devEUI, nwkKey, appKey);

    // activateOTAA é bloqueante (janelas RX) e usa o barramento SPI, que é
    // compartilhado com o display. Travamos o bus durante a transação para
    // a UI Task não corromper os dados do rádio (e vice-versa).
    int16_t joinState;
    {
      SpiBusGuard busGuard;
      digitalWrite(BOARD_TFT_CS,    HIGH);  // desseleciona display
      digitalWrite(BOARD_SDCARD_CS, HIGH);  // desseleciona SD
      joinState = node->activateOTAA();
    }

    if (joinState == RADIOLIB_LORAWAN_NEW_SESSION ||
        joinState == RADIOLIB_LORAWAN_SESSION_RESTORED) {
      Serial.println("[LoRaWAN] Conectado na TTN!");
      publishPhase(LoRaWANPhase::JOINED, subBand, joinState, attempts);

      // Sucesso: a Task dorme indefinidamente de forma cooperativa.
      // (Aqui você pode trocar por um loop de uplink periódico.)
      for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }

    // Falhou: mostra e rotaciona a sub-banda.
    Serial.printf("[LoRaWAN] Sem join na FSB %u (cod %d)\n", subBand, joinState);
    subBand = (subBand == 2) ? 1 : 2;
    publishPhase(LoRaWANPhase::WAITING, subBand, joinState, attempts);

    // Janela de espera de 20s NÃO-BLOQUEANTE: cede a CPU para as outras
    // Tasks (UI/GPS) durante toda a espera.
    vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL_MS));
  }
}
