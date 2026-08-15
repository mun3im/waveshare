#include <TFT_eSPI.h>
#include <SPI.h>


TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);  // Wait for initialization to succeed
  }
  Serial.println("Hello World!");
  tft.init();                 // Initialize the display
  tft.setRotation(1);         // Set rotation (optional)
  tft.fillScreen(TFT_BLACK);  // Clear the screen
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Hello, World!", 10, 10);  // Print a message
}

void loop() {}