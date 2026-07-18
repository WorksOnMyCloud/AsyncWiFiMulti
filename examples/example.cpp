#include <Arduino.h>
#include <AsyncWiFiMulti.h>

AsyncWiFiMulti wifiMulti;

void setup() {

  wifiMulti.addAP("ssid", "pass");

  wifiMulti.onConnected([](const AsyncWiFiMulti::ApSettings &ap) {
    printf("Connected to %02x:%02x:%02x:%02x:%02x:%02x %s\n",
    ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
    ap.ssid.c_str());
  });
  wifiMulti.onFailure([]() {
    printf("Failed to connect to any configured AP.\n");
    delay(1000);
    wifiMulti.start();
  });
  wifiMulti.onDisconnected([](const char *ssid, uint8_t disconnectionReason) {
    printf("Disconnected from %s reason: %d\n", ssid, disconnectionReason);
    delay(1000);
    wifiMulti.start();
  });

  wifiMulti.start();
}

void loop() {
}
