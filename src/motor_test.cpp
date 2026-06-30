// ВРЕМЕННЫЙ тест мотора (отдельная сборка `motortest`).
// Непрерывно шагает по STEP, периодически меняя DIR. EN — на GND (всегда вкл.).
// Сборка/заливка:  pio run -e motortest -t upload
// Вернуть рабочую прошивку:  pio run -t upload
#include <Arduino.h>
#include "config.h"

static const uint32_t STEP_HALF_US   = 700;   // полупериод STEP → ~714 шаг/с
static const uint32_t STEPS_PER_DIR  = 1600;  // через столько шагов меняем направление

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(STEP_STEP_PIN, OUTPUT);
  pinMode(STEP_DIR_PIN,  OUTPUT);
  digitalWrite(STEP_STEP_PIN, LOW);
  digitalWrite(STEP_DIR_PIN,  HIGH);
  Serial.println("\n=== MOTOR SPIN TEST ===");
  Serial.printf("STEP=GPIO%d  DIR=GPIO%d  (EN на GND)\n", STEP_STEP_PIN, STEP_DIR_PIN);
}

void loop() {
  static uint32_t count = 0;
  static bool dir = true;

  digitalWrite(STEP_STEP_PIN, HIGH);
  delayMicroseconds(STEP_HALF_US);
  digitalWrite(STEP_STEP_PIN, LOW);
  delayMicroseconds(STEP_HALF_US);

  if (++count >= STEPS_PER_DIR) {
    count = 0;
    dir = !dir;
    digitalWrite(STEP_DIR_PIN, dir ? HIGH : LOW);
    Serial.println(dir ? "DIR -> (CW)" : "DIR <- (CCW)");
  }
}
