#include "measurement_buffer.h"
#include "config.h"

// Armazenamento na PSRAM (alocado em bufferInit).
static Measurement*     buf      = nullptr;
static uint32_t         cap      = 0;     // capacidade
static uint32_t         head     = 0;     // índice do MAIS ANTIGO
static uint32_t         count    = 0;     // itens válidos
static SemaphoreHandle_t mtx     = nullptr;

bool bufferInit() {
  if (buf) return true;  // já inicializado

  mtx = xSemaphoreCreateMutex();
  cap = CFG_BUFFER_CAPACITY;

  // Aloca na PSRAM (ps_malloc). Se não houver PSRAM, cai para malloc normal
  // com um tamanho bem menor para não estourar a RAM interna.
  size_t bytes = (size_t)cap * sizeof(Measurement);
  buf = (Measurement*) ps_malloc(bytes);
  if (!buf) {
    // Fallback: sem PSRAM, usa um buffer pequeno na RAM interna.
    cap = 500;
    bytes = (size_t)cap * sizeof(Measurement);
    buf = (Measurement*) malloc(bytes);
  }
  head = count = 0;
  return buf != nullptr;
}

void bufferPush(const Measurement& m) {
  if (!buf) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  uint32_t tail = (head + count) % cap;   // próxima posição livre
  buf[tail] = m;
  if (count < cap) {
    count++;
  } else {
    // Cheio: sobrescreveu o mais antigo; avança o head (descarta antigo).
    head = (head + 1) % cap;
  }
  xSemaphoreGive(mtx);
}

uint32_t bufferCount() {
  if (!buf) return 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  uint32_t c = count;
  xSemaphoreGive(mtx);
  return c;
}

uint32_t bufferCapacity() { return cap; }

uint32_t bufferPeek(Measurement* out, uint32_t maxOut) {
  if (!buf || !out) return 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  uint32_t n = (count < maxOut) ? count : maxOut;
  for (uint32_t i = 0; i < n; i++) {
    out[i] = buf[(head + i) % cap];
  }
  xSemaphoreGive(mtx);
  return n;
}

void bufferPopFront(uint32_t n) {
  if (!buf) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (n > count) n = count;
  head  = (head + n) % cap;
  count -= n;
  xSemaphoreGive(mtx);
}

void bufferClear() {
  if (!buf) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  head = count = 0;
  xSemaphoreGive(mtx);
}
