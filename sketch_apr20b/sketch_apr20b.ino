#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  tft.init();
  tft.setRotation(1); // Adjust rotation if needed
  tft.fillScreen(TFT_WHITE);
  tft.setCursor(50, 50);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.println("Hello, ESP32-S3!");
}

void loop() {
  // Main loop
}