#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10); // Wait for initialization to succeed
  }
  Serial.println("Hello World!");

  tft.init(); // Initialize the display

  // Optionally, set the rotation
  tft.setRotation(1);

  // Display a test message
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Hello, World!", 10, 10);
}

void loop() {
  // Your main code here
}