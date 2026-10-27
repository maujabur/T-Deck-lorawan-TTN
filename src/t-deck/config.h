#pragma once
//
// config.h
// --------
// Parâmetros CONFIGURÁVEIS do firmware do T-Deck, agrupados num só lugar.
// Ajuste aqui sem precisar caçar valores espalhados pelo código.
//
// (Pinos de hardware ficam em board.h / gps_task.h / lorawan_task.h, pois são
//  fixos da placa; aqui ficam só os parâmetros de comportamento.)
//

// ============================================================
// RELÓGIO (exibição na tela)
// ============================================================
// Fuso horário para MOSTRAR o relógio do GPS na tela, em horas relativas
// ao UTC. Brasília = -3. (Os dados enviados ao ThingSpeak continuam em UTC;
// isto afeta só o relógio exibido.)
#define CFG_TZ_OFFSET_HOURS            -3

// ============================================================
// RANGE TEST — cadência de medição e envio
// ============================================================

// Intervalo entre medições (PING->ACK) quando HÁ internet (ms).
#define CFG_MEAS_INTERVAL_ONLINE_MS    4000UL

// Intervalo entre medições quando SEM internet (ms). Maior = economiza
// buffer e bateria durante trechos offline.
#define CFG_MEAS_INTERVAL_OFFLINE_MS   16000UL

// Timeout de espera do ACK depois de transmitir o PING (ms).
// 0 = deixa a RadioLib calcular pelo SF (padrão). Aumente se, de longe, o
// ACK chegar tarde e aparecer "SEM ACK" indevidamente.
#define CFG_ACK_TIMEOUT_MS             0

// ============================================================
// PROTEÇÃO DE BATERIA (descarga profunda)
// ============================================================
// O T-Deck tem um interruptor FÍSICO que corta a energia; mas se ele ficar
// ligado com a bateria no limite (ex.: esquecido no transporte), a célula
// Li-Ion pode descarregar demais. Esta proteção faz o firmware dormir sozinho.
//
// Abaixo de CFG_VBAT_SLEEP_V (Volts), por CFG_VBAT_LOW_COUNT leituras seguidas
// (cada ~5s) e NÃO carregando, o T-Deck desliga o rádio e o trilho de energia
// dos periféricos e entra em deep sleep. A contagem evita dormir num pico de
// queda durante um TX.
//
// NÃO configuramos fonte de wake: só o CICLO DA CHAVE FÍSICA (OFF->ON) religa.
// Assim um toque acidental (ex.: na bolinha) durante o transporte NÃO acorda o
// aparelho — coerente com proteger a bateria de verdade.
//
// Ao religar a chave, o boot relê a bateria: se ainda estiver abaixo de
// CFG_VBAT_WAKE_V (histerese, maior que a de dormir), avisa "CARREGUE" e volta
// a dormir. Como o T-Deck não detecta o USB diretamente, é a própria subida da
// tensão ao carregar que permite religar.
//
// Observação de design: a TELA APAGADA (tecla B) é uso discreto, NÃO dispara
// sono. O único gatilho é a bateria crítica.
//
// 0 em CFG_VBAT_SLEEP_V desativa a proteção.
#define CFG_VBAT_SLEEP_V               3.20f   // dorme abaixo disto
#define CFG_VBAT_WAKE_V                3.40f   // só religa acima disto (histerese)
#define CFG_VBAT_LOW_COUNT             3       // leituras seguidas p/ confirmar

// ============================================================
// TELEMETRIA / ThingSpeak
// ============================================================

// Intervalo entre descargas (flush) do buffer para o ThingSpeak quando
// online (ms). O plano gratuito exige >= 15s entre updates/bulks.
#define CFG_FLUSH_INTERVAL_MS          16000UL

// Máximo de pontos por bulk-update. Free do ThingSpeak aceita até 960.
// Deixamos uma folga de segurança.
#define CFG_BULK_MAX_POINTS            900

// Tempo para tentar conectar ao WiFi no início (ms).
#define CFG_WIFI_CONNECT_TIMEOUT_MS    15000UL

// Intervalo entre tentativas de RECONEXÃO ativa quando o WiFi cai (ms).
// Sem isto, após perder o WiFi uma vez (ex.: hotspot sumiu), o ESP não
// voltava a conectar sozinho ao reaparecer.
#define CFG_WIFI_RETRY_MS              10000UL

// ============================================================
// BUFFER DE MEDIÇÕES (PSRAM, circular)
// ============================================================

// Capacidade do buffer circular (nº de medições). Cada medição ocupa
// ~48 bytes, então 10000 ~= 480 KB na PSRAM (que tem 8 MB). Ao encher,
// as medições MAIS ANTIGAS são descartadas.
#define CFG_BUFFER_CAPACITY            10000

// ============================================================
// Observações
// ============================================================
// - Só medições COM horário de GPS válido entram no buffer (precisam de
//   timestamp absoluto para o bulk-update). Sem fix, a medição ainda conta
//   em seq/acks, mas não é enviada ao mapa.
// - Tudo isto só vale nos modos de range test (TEST_LORA_RANGE).
