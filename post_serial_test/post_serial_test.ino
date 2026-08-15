// #include "waveshare_lcd.h"
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();

void setup() {
  // Initialize 485 device
  Serial.begin(115200);
  while (!Serial) {
    delay(10);  // Wait for initialization to succeed
  }
  for (int i = 0; i < 10; i++)
    Serial.println("Hello World!");

  // waveshare_lcd_init();
  // tft.init();
}

void loop() {
  // Waiting for 485 data, cannot exceed 120 characters
  if (Serial.available()) {
    // Send the received data back
    Serial.write(Serial.read());
  }
}
