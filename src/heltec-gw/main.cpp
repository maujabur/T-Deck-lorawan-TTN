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
#include "range_proto.h"   // protocolo PING/ACK compartilhado

// ---- OLED ----
#define OLED_W 128
#define OLED_H 64
static Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, HELTEC_OLED_RST);

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

static void drawScreen(const char* statusLine) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("LoRa RX 915MHz");
  display.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.printf("Pacotes: %lu", (unsigned long)rxCount);

  display.setCursor(0, 26);
  if (rxCount > 0) {
    display.printf("RSSI:%.0f SNR:%.1f", lastRssi, lastSnr);
    display.setCursor(0, 38);
    display.println("Msg:");
    display.setCursor(0, 48);
    // Mostra ate ~20 chars da mensagem.
    display.println(lastMsg.substring(0, 20));
  } else {
    display.setCursor(0, 38);
    display.println(statusLine);
  }
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
  Serial.println("[GW] Escutando...");
  drawScreen("Escutando...");
}

void loop() {
  if (packetReceived) {
    // Desabilita a flag enquanto processa.
    enableRx = false;
    packetReceived = false;

    String msg;
    int16_t st = radio.readData(msg);

    if (st == RADIOLIB_ERR_NONE) {
      rxCount++;
      lastMsg  = msg;
      lastRssi = radio.getRSSI();
      lastSnr  = radio.getSNR();

      // Se for um PING do range test, responde um ACK com o RSSI/SNR que
      // medimos (qualidade do enlace de IDA). O T-Deck casa pelo seq.
      uint32_t seq = 0;
      if (rangeParsePing(msg.c_str(), &seq)) {
        char ack[48];
        rangeMakeAck(ack, sizeof(ack), seq, (int)lround(lastRssi), lastSnr);

        // Half-duplex: transmite o ACK e depois volta a escutar.
        int16_t txSt = radio.transmit(ack);
        Serial.printf("[GW] RX #%lu seq=%lu RSSI=%.1f SNR=%.1f -> ACK (tx %d)\n",
                      (unsigned long)rxCount, (unsigned long)seq,
                      lastRssi, lastSnr, txSt);
      } else {
        Serial.printf("[GW] RX #%lu  RSSI=%.1f dBm  SNR=%.1f dB  msg=\"%s\"\n",
                      (unsigned long)rxCount, lastRssi, lastSnr, msg.c_str());
      }
      drawScreen("");
    } else if (st == RADIOLIB_ERR_CRC_MISMATCH) {
      Serial.println("[GW] Pacote com CRC invalido");
    } else {
      Serial.printf("[GW] readData erro: %d\n", st);
    }

    // Volta a escutar.
    radio.startReceive();
    enableRx = true;
  }

  delay(5);
}
