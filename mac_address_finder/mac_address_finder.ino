#include <WiFi.h>
#include "esp_mac.h" // Native Espressif MAC library

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000);

  // Give Wi-Fi radio driver a moment to initialize
  WiFi.mode(WIFI_STA);
  delay(100);

  // Fetch MAC address directly from eFuse hardware storage
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  Serial.println("\n===========================================");
  Serial.printf("Standard MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.printf("C Array for ESP-NOW: {0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println("===========================================\n");
}

void loop() {
  // Nothing needed here
}