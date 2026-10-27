//
// Heltec WiFi LoRa 32 V3 — Receptor LoRa ponto-a-ponto (validador de rádio)
// -------------------------------------------------------------------------
// Primeiro firmware do "gateway" de bancada. Ainda NÃO é LoRaWAN: é um
// receptor LoRa cru que escuta no ar e mostra no OLED + serial o que chega,
// com RSSI e SNR. Serve para confirmar que o rádio do T-Deck transmite e que
// este Heltec recebe, antes de encarar o single-channel packet forwarder.
//
// Para o teste funcionar, o TRANSMISSOR (T-Deck em modo de teste) precisa
// usar EXATAMENTE os mesmos parâmetros de rádio definidos em heltec_board.h
// (frequência, BW, SF, CR, sync word, preâmbulo).
//
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>
#include "heltec_board.h"
#include "heltec_config.h"   // parâmetros de energia/bateria/display
#include "heltec_power.h"    // botão, bateria, deep sleep
#include "range_proto.h"     // protocolo PING/ACK compartilhado

// ---- OLED ----
#define OLED_W 128
#define OLED_H 64
static Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, HELTEC_OLED_RST);

// ---- Estado do display (economia) ----
static bool     displayOn      = true;
static uint32_t lastActivityMs = 0;    // última vez que algo "aconteceu"
static float    battVolts      = 0.0f; // tensão lida periodicamente

// Liga/desliga o painel OLED (comando do controlador; não mexe no Vext,
// para o rádio seguir funcionando).
// Imprime uma linha centralizada horizontalmente, no y dado.
static void printCentered(const char* line, int16_t y) {
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(line, 0, y, &x1, &y1, &w, &h);
  int16_t x = ((int16_t)display.width() - (int16_t)w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(line);
}

// Forward: setDisplayOn() redesenha a tela ao ligar; a definição vem depois.
static void drawScreen(const char* statusLine);

// Flash rápido de feedback. Quebra a mensagem por espaços (uma palavra por
// linha) e centraliza cada linha — em retrato (64px) "SCREEN OFF" não cabe
// numa linha só, então vira duas linhas limpas em vez de cortado no meio.
static void flashMessage(const char* msg, uint16_t ms) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Escolhe o tamanho da fonte pela MAIOR palavra: se alguma palavra não
  // couber na largura em fonte 2 (~12px/char), usa fonte 1 (~6px/char).
  char tmp[32];
  strncpy(tmp, msg, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  int nLines = 1, maxWord = 0, cur = 0;
  for (char* p = tmp; ; p++) {
    if (*p == ' ' || *p == '\0') {
      if (cur > maxWord) maxWord = cur;
      cur = 0;
      if (*p == ' ') nLines++;
      if (*p == '\0') break;
    } else cur++;
  }
  uint8_t sz = (maxWord * 12 <= (int)display.width()) ? 2 : 1;
  display.setTextSize(sz);
  const int16_t lineH = (sz == 2) ? 18 : 10;

  int16_t y = ((int16_t)display.height() - nLines * lineH) / 2;
  if (y < 0) y = 0;

  strncpy(tmp, msg, sizeof(tmp) - 1);   // strtok consome; recarrega
  tmp[sizeof(tmp) - 1] = '\0';
  char* tok = strtok(tmp, " ");
  while (tok) {
    printCentered(tok, y);
    y += lineH;
    tok = strtok(nullptr, " ");
  }

  display.display();
  delay(ms);
  display.setTextSize(1);
}

// Liga/desliga o painel OLED. Se 'reason' != nullptr, dá um flash de
// feedback com o motivo antes de apagar (ou ao religar).
static void setDisplayOn(bool on, const char* reason = nullptr) {
  if (!on) {
    // Vai apagar: mostra o motivo por um instante, depois desliga o painel.
    if (reason) flashMessage(reason, HCFG_FLASH_MS);
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  } else {
    // Vai ligar: liga o painel, dá o flash de feedback e já redesenha a tela
    // de status na hora. Sem este redraw imediato, o "SCREEN ON" ficava
    // congelado até o próximo ciclo da loop() (que espera o rádio), parecendo
    // mais longo que os outros flashes.
    display.ssd1306_command(SSD1306_DISPLAYON);
    if (reason) flashMessage(reason, HCFG_FLASH_MS);
    lastActivityMs = millis();
    displayOn = on;
    drawScreen("Escutando");
    return;
  }
  displayOn = on;
}

// ---- Rádio SX1262 ----
static SX1262 radio = new Module(HELTEC_LORA_NSS, HELTEC_LORA_DIO1,
                                 HELTEC_LORA_RST, HELTEC_LORA_BUSY);

// Flag setada na interrupção (DIO1) quando um pacote chega. volatile: ISR.
static volatile bool packetReceived = false;
static volatile bool enableRx       = true;

// Contadores e último pacote (para a tela).
static uint32_t rxCount = 0;
static String   lastMsg = "";
static float    lastRssi = 0.0f;
static float    lastSnr  = 0.0f;

// ISR: executada no contexto de interrupção. Mantenha curtíssima.
#if defined(ESP32)
IRAM_ATTR
#endif
static void onDio1() {
  if (!enableRx) return;
  packetReceived = true;
}

// Liga o trilho Vext (ativo em LOW) para alimentar o OLED.
static void powerOnPeripherals() {
  pinMode(HELTEC_VEXT, OUTPUT);
  digitalWrite(HELTEC_VEXT, LOW);
  delay(50);
  // Pulso de reset do OLED.
  pinMode(HELTEC_OLED_RST, OUTPUT);
  digitalWrite(HELTEC_OLED_RST, LOW);
  delay(20);
  digitalWrite(HELTEC_OLED_RST, HIGH);
  delay(20);
}

// Imprime 'txt' alinhado à DIREITA na linha y (usa a largura real do texto).
static void printRight(int16_t y, const char* txt) {
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  display.setCursor(display.width() - w, y);
  display.print(txt);
}

static void drawScreen(const char* statusLine) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Layout RETRATO (~64 px de largura). Rótulo à esquerda, VALOR à direita.
  const int16_t W = display.width();
  const int16_t H = display.height();
  char buf[16];

  // Cabeçalho.
  display.setCursor(0, 0);
  display.println("LoRa RX");
  display.setCursor(0, 9);
  display.println("915 MHz");
  display.drawFastHLine(0, 19, W, SSD1306_WHITE);

  // Pacotes: rótulo à esquerda, número à direita.
  display.setCursor(0, 23);
  display.print("Pac");
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)rxCount);
  printRight(23, buf);

  if (rxCount > 0) {
    display.setCursor(0, 35);
    display.print("RSSI");
    snprintf(buf, sizeof(buf), "%.0f", lastRssi);
    printRight(35, buf);

    display.setCursor(0, 47);
    display.print("SNR");
    snprintf(buf, sizeof(buf), "%.1f", lastSnr);
    printRight(47, buf);

    display.setCursor(0, 59);
    display.print("Seq");
    // Só o número do seq (a msg é "PING,N" -> pega o que vem após a vírgula).
    const char* comma = strchr(lastMsg.c_str(), ',');
    snprintf(buf, sizeof(buf), "%s", comma ? comma + 1 : lastMsg.c_str());
    printRight(59, buf);

    // Conteúdo bruto do último pacote, abaixo do Seq. Pode enrolar para a
    // próxima linha (textWrap default) se for longo — tudo bem, cabe até o
    // rodapé da bateria.
    display.setCursor(0, 71);
    display.print("Msg:");
    display.setCursor(0, 81);
    display.print(lastMsg);
  } else {
    display.setCursor(0, 35);
    display.println(statusLine);
  }

  // Rodapé: bateria (rótulo esquerda, valor direita).
  display.drawFastHLine(0, H - 12, W, SSD1306_WHITE);
  display.setCursor(0, H - 8);
  display.print("Bat");
  if (battVolts > 0.1f) snprintf(buf, sizeof(buf), "%.2fV", battVolts);
  else                  snprintf(buf, sizeof(buf), "--");
  printRight(H - 8, buf);
  display.display();
}

// ---- Proteção de bateria (descarga profunda) ----
// Entra em deep sleep com um aviso na tela. Chamado tanto na confirmação do
// loop quanto ao acordar com bateria ainda fraca.
static void batterySleepNow(float v) {
  Serial.printf("[PWR] Bateria fraca (%.2f V) -> deep sleep de protecao.\n", v);
  display.ssd1306_command(SSD1306_DISPLAYON);
  flashMessage("BATERIA FRACA", HCFG_FLASH_MS);
  flashMessage("CARREGUE", HCFG_FLASH_MS);
  display.clearDisplay(); display.display();
  radio.sleep();
  powerDeepSleep();   // não retorna; acorda pelo botão (reboot)
}

// Verifica no loop se a bateria caiu abaixo do limiar por N leituras seguidas.
// Só conta leituras plausíveis (> 0.1 V): se o ADC vier zerado (sem bateria /
// alimentado por USB sem célula) não disparamos a proteção.
static void batteryProtectTick(float v) {
  if (HCFG_VBAT_SLEEP_V <= 0.0f) return;          // proteção desativada
  static uint8_t lowCount = 0;
  if (v > 0.1f && v < HCFG_VBAT_SLEEP_V) {
    if (++lowCount >= HCFG_VBAT_LOW_COUNT) batterySleepNow(v);
  } else {
    lowCount = 0;                                 // recuperou: zera a contagem
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[GW] Heltec V3 - Receptor LoRa ponto-a-ponto");

  // 1) Energia + OLED.
  powerOnPeripherals();
  Wire.begin(HELTEC_OLED_SDA, HELTEC_OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[GW] OLED SSD1306 nao encontrado (0x3C)!");
    // Segue mesmo assim; o rádio é o que importa.
  } else {
    display.setRotation(HCFG_DISPLAY_ROTATION);   // retrato para o case em pé
    // Se acordou de deep sleep (botão), dá um flash de "WAKE".
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
      flashMessage("WAKE", HCFG_FLASH_MS);
    }
    drawScreen("Iniciando");
  }

  // 2) SPI do rádio (barramento dedicado do Heltec).
  SPI.begin(HELTEC_LORA_SCK, HELTEC_LORA_MISO, HELTEC_LORA_MOSI, HELTEC_LORA_NSS);

  // 3) Inicializa o SX1262 com os parâmetros de teste.
  int16_t st = radio.begin(HELTEC_RADIO_FREQ, HELTEC_RADIO_BW,
                           HELTEC_RADIO_SF, HELTEC_RADIO_CR,
                           HELTEC_RADIO_SYNC, HELTEC_RADIO_TXPOWER,
                           HELTEC_RADIO_PREAMBLE);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[GW] radio.begin falhou: %d\n", st);
    drawScreen("ERRO radio");
    while (true) { delay(1000); }
  }

  // 4) Registra a interrupção de recepção e entra em modo escuta.
  radio.setDio1Action(onDio1);
  st = radio.startReceive();
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[GW] startReceive falhou: %d\n", st);
  }
  // 5) Energia: botão PRG (GPIO0) + leitura de bateria.
  powerInit();
  battVolts = powerReadBattery();
  lastActivityMs = millis();

  // Proteção ao ACORDAR: se a bateria ainda estiver fraca (abaixo da histerese
  // de religar), volta a dormir em vez de ligar só para desligar de novo.
  // Só vale quando há bateria plausível (> 0.1 V): alimentado por USB sem
  // célula o ADC pode vir baixo e não queremos dormir nesse caso.
  if (HCFG_VBAT_SLEEP_V > 0.0f && battVolts > 0.1f && battVolts < HCFG_VBAT_WAKE_V) {
    batterySleepNow(battVolts);
  }

  Serial.println("[GW] Escutando...");
  drawScreen("Escutando");
}

void loop() {
  // ---- Botão PRG (GPIO0) ----
  BtnEvent be = powerPollButton();
  if (be == BtnEvent::SHORT) {
    // Clique curto: liga/desliga o OLED (economia leve; rádio segue ativo).
    if (displayOn) setDisplayOn(false, "SCREEN OFF");
    else           setDisplayOn(true,  "SCREEN ON");
    Serial.printf("[PWR] OLED %s\n", displayOn ? "ligado" : "desligado");
  } else if (be == BtnEvent::LONG) {
    // Pressão longa: deep sleep (ultra econômico). Desliga rádio e OLED.
    Serial.println("[PWR] Entrando em deep sleep...");
    // Garante o painel ligado para o flash "SLEEP" aparecer mesmo quando a
    // tela já estava apagada (senão o sleep ficava sem feedback nenhum).
    display.ssd1306_command(SSD1306_DISPLAYON);
    flashMessage("SLEEP", HCFG_FLASH_MS);        // feedback antes de dormir
    display.clearDisplay(); display.display();
    radio.sleep();
    powerDeepSleep();   // não retorna; acorda pelo botão (reboot)
  }

  // ---- Recepção de pacote ----
  if (packetReceived) {
    enableRx = false;
    packetReceived = false;

    String msg;
    int16_t st = radio.readData(msg);

    if (st == RADIOLIB_ERR_NONE) {
      rxCount++;
      lastMsg  = msg;
      lastRssi = radio.getRSSI();
      lastSnr  = radio.getSNR();
      // NÃO reinicia o timeout do display aqui: o auto-sleep conta só a partir
      // da última interação do BOTÃO. Durante um range test o T-Deck pinga a
      // cada poucos segundos; se o tráfego de rádio contasse como atividade, o
      // OLED nunca apagaria. Com a tela apagada o rádio/ACK seguem normais.

      uint32_t seq = 0;
      if (rangeParsePing(msg.c_str(), &seq)) {
        char ack[48];
        rangeMakeAck(ack, sizeof(ack), seq, (int)lround(lastRssi), lastSnr);
        int16_t txSt = radio.transmit(ack);
        Serial.printf("[GW] RX #%lu seq=%lu RSSI=%.1f SNR=%.1f -> ACK (tx %d)\n",
                      (unsigned long)rxCount, (unsigned long)seq,
                      lastRssi, lastSnr, txSt);
      } else {
        Serial.printf("[GW] RX #%lu  RSSI=%.1f dBm  SNR=%.1f dB  msg=\"%s\"\n",
                      (unsigned long)rxCount, lastRssi, lastSnr, msg.c_str());
      }
      if (displayOn) drawScreen("");
    } else if (st == RADIOLIB_ERR_CRC_MISMATCH) {
      Serial.println("[GW] Pacote com CRC invalido");
    } else {
      Serial.printf("[GW] readData erro: %d\n", st);
    }

    radio.startReceive();
    enableRx = true;
  }

  // ---- Leitura periódica da bateria + refresh do OLED (~a cada 5s) ----
  static uint32_t lastBatMs = 0;
  if (millis() - lastBatMs > 5000) {
    lastBatMs = millis();
    float rawMv = powerReadBatteryRawMv();
    battVolts = (rawMv * HCFG_VBAT_FACTOR) / 1000.0f;
    // Diagnóstico: valor bruto no pino. Se ficar ~0, o pino/controle do ADC
    // estão errados (ou não há bateria conectada).
    Serial.printf("[BAT] raw=%.0f mV  -> %.2f V (fator %.2f)\n",
                  rawMv, battVolts, HCFG_VBAT_FACTOR);
    if (displayOn) drawScreen("Escutando");

    // Proteção de descarga profunda: dorme se ficar abaixo do limiar por
    // HCFG_VBAT_LOW_COUNT leituras seguidas.
    batteryProtectTick(battVolts);
  }

  // ---- Auto-sleep do display por inatividade ----
  if (displayOn && HCFG_DISPLAY_TIMEOUT_MS > 0 &&
      (millis() - lastActivityMs > HCFG_DISPLAY_TIMEOUT_MS)) {
    setDisplayOn(false, "SCREEN OFF");
    Serial.println("[PWR] OLED dormiu por inatividade (botao religa).");
  }

  delay(5);
}
