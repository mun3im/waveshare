#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  tft.begin();
  delay(1000);
  tft.setRotation(1);
  tft.fillScreen(TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("ILI9806E Ready!", 50, 50, 4);

  if (psramFound()) {
    Serial.println("PSRAM detected!");
    Serial.printf("Total PSRAM: %d bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("PSRAM NOT FOUND!");
  }
}

void loop() {
}
