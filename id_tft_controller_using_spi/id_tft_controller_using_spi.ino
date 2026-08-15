#include <SPI.h>

#define TFT_CS   15  // Replace with your actual CS pin
#define TFT_DC   2   // Replace with your actual DC pin

void sendCommand(uint8_t cmd) {
  digitalWrite(TFT_DC, LOW);
  digitalWrite(TFT_CS, LOW);
  SPI.transfer(cmd);
  digitalWrite(TFT_CS, HIGH);
}

uint32_t readDisplayID() {
  digitalWrite(TFT_CS, LOW);
  digitalWrite(TFT_DC, LOW);
  SPI.transfer(0x04);  // Read Display ID command

  digitalWrite(TFT_DC, HIGH);
  // Dummy read (first byte is usually dummy)
  SPI.transfer(0x00);
  uint8_t b1 = SPI.transfer(0x00);
  uint8_t b2 = SPI.transfer(0x00);
  uint8_t b3 = SPI.transfer(0x00);
  digitalWrite(TFT_CS, HIGH);

  return (b1 << 16) | (b2 << 8) | b3;
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);

  uint32_t id = readDisplayID();
  Serial.print("Display ID = 0x");
  Serial.println(id, HEX);
}

void loop(){}
