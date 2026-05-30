/*
 * LifeLine — Wokwi (paste with diagram.json from repo wokwi/ folder)
 *
 * After paste: Save → Simulate. Do NOT drag wires on the diagram.
 *
 * Breadboard / ESP-IDF (firmware/main/config.h):
 *   WS2812B x8      DIN = 38    5V
 *   INMP441         SD=2  SCK=41  WS=42  L/R=GND
 *   MAX98357A       DIN=7  BCLK=5  LRC=6  SD=10
 *   MAX30102        SDA=8  SCL=9  INT=4
 */

#include <Adafruit_NeoPixel.h>

#define PIN_NEO  38
#define PIN_BEEP 7
#define NUMPIX   8

Adafruit_NeoPixel ring(NUMPIX, PIN_NEO, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("LifeLine Wokwi — check wiring in diagram.json");
  Serial.println("NeoPixel=38  Pot SIG=2  Buzzer=7  I2C 8/9 INT=4");
  Serial.println("If the ring stays dark, diagram.json was not pasted fully.");

  pinMode(PIN_BEEP, OUTPUT);
  ring.begin();
  ring.setBrightness(80);
  ring.show();
}

static void color(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUMPIX; i++) {
    ring.setPixelColor(i, ring.Color(r, g, b));
  }
  ring.show();
}

void loop() {
  Serial.println("IDLE");
  color(0, 80, 0);
  noTone(PIN_BEEP);
  delay(1500);

  Serial.println("RECORDING");
  color(220, 0, 0);
  tone(PIN_BEEP, 880, 80);
  delay(1500);

  Serial.println("PROCESSING");
  color(90, 0, 140);
  noTone(PIN_BEEP);
  delay(1500);

  Serial.println("SPEAKING");
  color(0, 130, 50);
  tone(PIN_BEEP, 523, 200);
  delay(200);
  noTone(PIN_BEEP);
  delay(1300);
}
