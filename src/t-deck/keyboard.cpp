#include "keyboard.h"
#include <Wire.h>

static bool kbDetected = false;

bool keyboardInit() {
  // O teclado compartilha o I2C com outros periféricos; inicializa o bus.
  Wire.begin(KB_I2C_SDA, KB_I2C_SCL);

  // Detecta o teclado tentando algumas leituras (o C3 pode demorar a subir).
  int retry = 5;
  while (retry-- > 0) {
    Wire.requestFrom(KB_I2C_ADDR, 1);
    if (Wire.available()) {
      Wire.read();           // descarta o byte (pode ser 0 = nenhuma tecla)
      kbDetected = true;
      break;
    }
    delay(50);
  }
  return kbDetected;
}

bool keyboardPresent() {
  return kbDetected;
}

char keyboardRead() {
  if (!kbDetected) return 0;

  // Pergunta ao teclado qual tecla foi pressionada. O protocolo da LilyGO
  // devolve 1 byte: 0 = nenhuma, ou o código ASCII da tecla.
  if (Wire.requestFrom(KB_I2C_ADDR, 1) != 1) return 0;
  if (!Wire.available()) return 0;

  char c = (char)Wire.read();
  return c;   // 0 se nada; o firmware do teclado só reporta uma vez por toque
}

// ---- Trackball ----
// Guarda o último nível de cada pino para detectar transição (borda).
static bool tbInit = false;
static int  tbLast[4];
static const uint8_t tbPins[4] = { TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT };

void trackballInit() {
  for (int i = 0; i < 4; i++) {
    pinMode(tbPins[i], INPUT_PULLUP);
    tbLast[i] = digitalRead(tbPins[i]);
  }
  pinMode(TB_CLICK, INPUT_PULLUP);
  tbInit = true;
}

TrackballDir trackballRead() {
  if (!tbInit) return TrackballDir::NONE;

  // Clique tem prioridade (BOOT em nível baixo = pressionado).
  if (digitalRead(TB_CLICK) == LOW) {
    return TrackballDir::CLICK;
  }

  // Cada eixo do trackball pulsa (muda de nível) ao girar. Detecta a borda.
  TrackballDir dirs[4] = { TrackballDir::UP, TrackballDir::DOWN,
                           TrackballDir::LEFT, TrackballDir::RIGHT };
  for (int i = 0; i < 4; i++) {
    int now = digitalRead(tbPins[i]);
    if (now != tbLast[i]) {
      tbLast[i] = now;
      return dirs[i];
    }
  }
  return TrackballDir::NONE;
}
