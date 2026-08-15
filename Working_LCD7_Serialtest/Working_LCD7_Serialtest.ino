void setup() {
  // Initialize 485 device
  Serial.begin(115200);
  while (!Serial) {
    delay(10); // Wait for initialization to succeed
  }
  Serial.println("Hello World!");
}

void loop() {
  // Waiting for 485 data, cannot exceed 120 characters
  if (Serial.available()) {
    // Send the received data back
    Serial.write(Serial.read());
  }
}
