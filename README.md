# T-Deck LoRaWAN TTN + Range Test

Firmware para o **LilyGO T-Deck** (ESP32-S3 + SX1262 + ST7789 + GPS ATGM336H)
com um companheiro **Heltec WiFi LoRa 32 V3** usado como receptor/gateway de
bancada. O projeto reúne, no mesmo repositório, vários firmwares selecionáveis
por *environment* do PlatformIO.

## Hardware

- **LilyGO T-Deck** — ESP32-S3, rádio LoRa SX1262, display ST7789 240x320,
  GPS ATGM336H, teclado QWERTY (I2C) e trackball, bateria Li-Ion.
- **Heltec WiFi LoRa 32 V3** — ESP32-S3, SX1262, OLED SSD1306. Receptor de
  bancada para testes de alcance.

Ambos usam o rádio **SX1262** em **915 MHz** (região AU915 / ISM BR).

## Estrutura do repositório

```
src/
  common/      Código e headers compartilhados entre firmwares
               - lora_test_params.h  parâmetros de rádio cru (freq/SF/BW/...)
               - range_proto.h       protocolo PING/ACK do range test
               - secrets.example.h   template de credenciais (versionado)
               - secrets.h           credenciais reais (NO .gitignore)
  t-deck/      Firmware do T-Deck (scanner LoRaWAN, GPS, tela, bateria,
               teclado, telemetria, modos de teste)
  heltec-gw/   Firmware do Heltec (receptor LoRa / responde ACK)
```

Cada environment compila apenas a sua pasta (via `build_src_filter`) mais a
`common/`.

## Credenciais (secrets.h)

As credenciais de WiFi e a chave do ThingSpeak **não** são versionadas.
Copie o template e preencha:

```
cp src/common/secrets.example.h src/common/secrets.h
# edite src/common/secrets.h com WiFi + ThingSpeak Write API Key
```

O `secrets.h` está no `.gitignore`.

## Environments / modos

Compilar e enviar com `pio run -e <env> -t upload`.

| Environment             | Placa   | O que faz |
|-------------------------|---------|-----------|
| `t-deck`                | T-Deck  | Scanner LoRaWAN AU915 (rotaciona FSB2↔FSB1), GPS, bateria na tela |
| `t-deck-txtest`         | T-Deck  | Transmite PING LoRa cru periódico (valida o rádio TX) |
| `t-deck-rangetest`      | T-Deck  | Range test bidirecional (PING→ACK), mostra ida/volta na tela |
| `t-deck-rangetest-wifi` | T-Deck  | Range test + telemetria WiFi para o ThingSpeak |
| `t-deck-kbtest`         | T-Deck  | Teste do teclado I2C + trackball (serial/tela) |
| `heltec-gw`             | Heltec  | Receptor LoRa: responde ACK aos PINGs do T-Deck |

### Uso típico do range test

1. Grave `heltec-gw` no Heltec e deixe-o fixo num ponto alto (alimentado por
   powerbank). Ele responde ACK automaticamente.
2. Grave `t-deck-rangetest-wifi` no T-Deck e leve com você.
3. Ligue o hotspot do celular (SSID/senha no `secrets.h`) para a telemetria.
4. Ande pela área e observe na tela do T-Deck: **ENLACE OK / SEM ACK**,
   qualidade de ida e volta, tempo desde o último ACK e coordenadas do GPS.
5. **Teclado:** marque pontos de interesse (ver abaixo). As marcas vão para o
   ThingSpeak com a posição do momento.

## Teclado do T-Deck (mapa descoberto)

- Teclas de letra/número → ASCII normal (I2C, endereço `0x55`).
- **SHIFT** → maiúsculas/alternativas. **SYM** → símbolos (ex.: `sym+mic = 0`).
- **ALT** → tratado dentro do controlador do teclado; **não** gera byte pelo
  I2C (ex.: `alt+b` liga/desliga o backlight mas ainda publica `b`). Não dá
  para distinguir combinações com ALT no nosso firmware.
- **Trackball** → 4 direções lidas por GPIO (UP/DOWN/LEFT/RIGHT).

### Marcação de waypoints (modo range test)

| Tecla   | Rótulo |
|---------|--------|
| espaço  | 0 (marca genérica) |
| w e r s d f z x c | 1 2 3 4 5 6 7 8 9 |

Cada marca aparece na tela (faixa azul "MARCA #N") e, com telemetria ligada,
é enviada imediatamente ao ThingSpeak.

## ThingSpeak (telemetria)

O T-Deck posta via HTTP (hotspot do celular). Campos usados:

| Campo | Conteúdo |
|-------|----------|
| field1 | RSSI ida (dBm, medido pelo Heltec) |
| field2 | SNR ida (dB) |
| field3 | RSSI volta (dBm, medido pelo T-Deck) |
| field4 | SNR volta (dB) |
| field5 | seq (contador de pings) |
| field6 | acks (ACKs recebidos) |
| field7 | waypoint: rótulo da marca |
| field8 | waypoint: contador de marcas |
| latitude / longitude | posição (quando há fix GPS) |

> **Atenção:** crie os campos 5–8 nas *Channel Settings* do ThingSpeak,
> senão o POST é aceito mas esses valores são descartados.

## Notas de hardware importantes

- **TFT_eSPI + Arduino core:** o TFT_eSPI 2.5.43 não funciona no Arduino core
  3.x (ESP-IDF 5), causando *Core Panic* no `spi.beginTransaction`. A
  plataforma está fixada em `espressif32@6.9.0` (core 2.x). No ESP32-S3 é
  necessário `-D USE_HSPI_PORT` e `-D TFT_RGB_ORDER=TFT_BGR` para o painel.
- **SPI compartilhado:** no T-Deck o display e o rádio dividem o mesmo
  barramento SPI; o rádio reaproveita a instância HSPI do TFT_eSPI e o acesso
  é serializado por mutex.
- **GPS:** RX=44 / TX=43 (TX/RX cruzados na solda desta placa). O primeiro
  fix (*cold start*) exige vários minutos a céu aberto, imóvel.
- **Serial de debug:** no T-Deck via USB-CDC nativo; no Heltec V3.2 via UART0
  (ponte CP210x), por isso o Heltec usa `ARDUINO_USB_CDC_ON_BOOT=0`.

## Licença

Projeto pessoal de experimentação. Sem garantia.
