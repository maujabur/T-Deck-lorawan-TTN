#include "lorawan_task.h"
#include "app_state.h"
#include "board.h"
#include "screen_ui.h"
#include <SPI.h>

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

  int16_t state = radio.begin();
  return state;
}

void loraTask(void* pvParameters) {
  (void)pvParameters;

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
