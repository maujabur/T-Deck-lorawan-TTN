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
#include "range_proto.h"        // protocolo PING/ACK
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

void loraTask(void* pvParameters) {
  (void)pvParameters;

#ifdef TEST_LORA_RANGE
  // ===== MODO RANGE TEST BIDIRECIONAL =====
  // T-Deck transmite PING, troca para RX e espera o ACK do Heltec. O ACK traz
  // o RSSI/SNR de IDA (medido pelo Heltec); o RSSI/SNR de VOLTA é medido aqui.
  // Mostra tudo na tela para teste de alcance a pé, sem TTN nem internet.
  uint32_t seq = 0;
  for (;;) {
    seq++;
    char ping[24];
    rangeMakePing(ping, sizeof(ping), seq);

    bool  gotAck   = false;
    int   rssiFwd  = 0;      // IDA (vem no ACK)
    float snrFwd   = 0.0f;
    int   rssiRev  = 0;      // VOLTA (medido aqui no ACK)
    float snrRev   = 0.0f;

    {
      SpiBusGuard busGuard;
      digitalWrite(BOARD_TFT_CS,    HIGH);
      digitalWrite(BOARD_SDCARD_CS, HIGH);

      // 1) Transmite o PING.
      int16_t txSt = radio.transmit(ping);

      if (txSt == RADIOLIB_ERR_NONE) {
        // 2) Escuta o ACK com timeout (bloqueante, mas nesta Task dedicada).
        String resp;
        int16_t rxSt = radio.receive(resp, 0);  // 0 = usa timeout padrão do SF
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

    Serial.printf("[RANGE] seq=%lu ack=%d  IDA rssi=%d snr=%.1f  VOLTA rssi=%d snr=%.1f\n",
                  (unsigned long)seq, gotAck ? 1 : 0,
                  rssiFwd, snrFwd, rssiRev, snrRev);

    // Publica no estado para a UI desenhar.
    {
      AppStateGuard guard;
      g_state.rtSeq     = seq;
      g_state.rtGotAck  = gotAck;
      if (gotAck) {
        g_state.rtAcks++;
        g_state.rtRssiFwd   = rssiFwd;
        g_state.rtSnrFwd    = snrFwd;
        g_state.rtRssiRev   = rssiRev;
        g_state.rtSnrRev    = snrRev;
        g_state.rtLastAckMs = millis();
      }
      g_state.phase = gotAck ? LoRaWANPhase::JOINED : LoRaWANPhase::WAITING;
      g_state.dirty = true;
    }

    // ~2s entre pings. Não-bloqueante (cede CPU para UI/GPS).
    vTaskDelay(pdMS_TO_TICKS(2000));
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
