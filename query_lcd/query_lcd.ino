#include <TFT_eSPI.h>
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.print("Hello World\n\n\n\n\n");

  // Initialize the display
  tft.init();
  tft.setRotation(1); // Adjust rotation if needed

  // Clear the screen
  tft.fillScreen(TFT_BLACK);

  // Print a message to the screen
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 10);
  tft.println("Querying Driver IC...");

  // Query the driver ID
  uint32_t driverID = readDriverID();
  Serial.print("Driver ID: 0x");
  Serial.println(driverID, HEX);

  // Display the result on the screen
  tft.setCursor(10, 30);
  tft.print("Driver ID: 0x");
  tft.println(driverID, HEX);
}

void loop() {
  Serial.println("Hello World!");
  delay(1000);
  // Nothing to do here
}

uint32_t readDriverID() {
  uint32_t id = 0;

  // Begin SPI transaction
  tft.startWrite();

  // Send the Read ID command (0x04)
  tft.writecommand(0x04);

  // Read three bytes of data
  uint8_t data1 = tft.readcommand8(0x04); // First byte
  uint8_t data2 = tft.readcommand8(0x04); // Second byte
  uint8_t data3 = tft.readcommand8(0x04); // Third byte

  // Combine bytes into a single 24-bit value
  id |= data1 << 16;
  id |= data2 << 8;
  id |= data3;

  // End SPI transaction
  tft.endWrite();

  return id;
}