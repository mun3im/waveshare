#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();  // Create object

void setup() {
  Serial.begin(115200);
  tft.init();

  uint8_t id = tft.readcommand8(0x04);  // Read Display ID
  Serial.print("Display ID: 0x");
  Serial.println(id, HEX);
}
