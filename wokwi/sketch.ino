// First Aid Assistant - Wokwi
// Gercek pinler: WS2812B=38 INMP441=2,41,42 MAX98357=5,6,7,10 MAX30102=8,9,4
// Wokwi: neo=17 (halka), pot=mic GPIO2, buzzer=spk, mpu6050=MAX30102 gorunumu I2C 8,9

#define NEO_PIN 17
#define BUZZ_PIN 18

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("First Aid Assistant");
  Serial.println("WS2812B x8  -> GPIO 38");
  Serial.println("INMP441    -> 2, 41, 42");
  Serial.println("MAX98357A  -> 5, 6, 7, 10");
  Serial.println("MAX30102   -> 8, 9, 4");
  Serial.println("PC         -> gui.py");
}

void loop() {
  Serial.println("IDLE");
  rgbLedWrite(NEO_PIN, 0, 80, 0);
  delay(1200);

  Serial.println("RECORDING");
  rgbLedWrite(NEO_PIN, 200, 0, 0);
  tone(BUZZ_PIN, 880, 80);
  delay(1200);

  Serial.println("PROCESSING");
  rgbLedWrite(NEO_PIN, 80, 0, 120);
  delay(1200);

  Serial.println("SPEAKING");
  rgbLedWrite(NEO_PIN, 0, 120, 40);
  tone(BUZZ_PIN, 523, 200);
  delay(300);
  noTone(BUZZ_PIN);
  delay(900);
}
