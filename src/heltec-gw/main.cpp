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
static void setDisplayOn(bool on) {
  displayOn = on;
  display.ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
  if (on) lastActivityMs = millis();
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
    drawScreen("Iniciando radio...");
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
    drawScreen("ERRO no radio!");
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

  Serial.println("[GW] Escutando...");
  drawScreen("Escutando...");
}

void loop() {
  // ---- Botão PRG (GPIO0) ----
  BtnEvent be = powerPollButton();
  if (be == BtnEvent::SHORT) {
    // Clique curto: liga/desliga o OLED (economia leve; rádio segue ativo).
    setDisplayOn(!displayOn);
    Serial.printf("[PWR] OLED %s\n", displayOn ? "ligado" : "desligado");
  } else if (be == BtnEvent::LONG) {
    // Pressão longa: deep sleep (ultra econômico). Desliga rádio e OLED.
    Serial.println("[PWR] Entrando em deep sleep...");
    if (displayOn) { display.clearDisplay(); display.display(); }
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
      lastActivityMs = millis();   // atividade: reinicia o timeout do display

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
    if (displayOn) drawScreen("Escutando...");
  }

  // ---- Auto-sleep do display por inatividade ----
  if (displayOn && HCFG_DISPLAY_TIMEOUT_MS > 0 &&
      (millis() - lastActivityMs > HCFG_DISPLAY_TIMEOUT_MS)) {
    setDisplayOn(false);
    Serial.println("[PWR] OLED dormiu por inatividade (botao religa).");
  }

  delay(5);
}
