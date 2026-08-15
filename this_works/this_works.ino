#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  tft.init(); // Initialize the display
  tft.setRotation(1); // Set rotation (optional)
  tft.fillScreen(TFT_BLACK); // Clear the screen
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Hello, World!", 10, 10); // Print a message
}

void loop() {}