#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "CommunicationUtils.h"
#include "BatteryService.h"
#include "ConfigurationService.h"
#include "InternalLedService.h"
#include "LedService.h"
#include "StatusService.h"
#include "StreamingService.h"
#include "config.h"

WebServer server(80);
ConfigurationService configuration;
LedService leds;
BatteryService battery;
InternalLedService internalLed;
StreamingService streaming;
StatusService status(leds, streaming, battery, internalLed);


// Funzione di supporto per convertire il valore numerico della modalità in testo leggibile
const char* getWiFiModeString(wifi_mode_t mode) {
  switch (mode) {
    case WIFI_OFF:     return "WIFI_OFF (Spento)";
    case WIFI_STA:     return "WIFI_STA (Station / Client)";
    case WIFI_AP:      return "WIFI_AP (Access Point)";
    case WIFI_AP_STA:  return "WIFI_AP_STA (Access Point + Station)";
    default:           return "SCONOSCIUTO";
  }
}


void setup() {
  Serial.begin(115200);
  configuration.begin();

  battery.begin();
  internalLed.begin(battery);
  leds.begin(configuration.storage());
  if (!streaming.beginCamera()) {
    Serial.println("Camera initialization failed");
    while (true) {
      delay(1000);
    }
  }

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  esp_err_t wifiPowerSaveResult = ESP_OK;
  bool wifiPowerSaveConfigured = false;
#if WIFI_DISABLE_POWER_SAVE
  wifiPowerSaveResult = esp_wifi_set_ps(WIFI_PS_NONE);
  wifiPowerSaveConfigured = true;
#endif
  esp_err_t wifiInactiveTimeResult =
      esp_wifi_set_inactive_time(WIFI_IF_AP, AP_INACTIVE_TIMEOUT_SECONDS);
  Serial.println(WiFi.softAPIP());

  Serial.println("\n=== CONFIGURAZIONE E SPECIFICHE ESP32 ===");
  
  // Modello e Revisione del Chip
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  Serial.printf("Modello Chip: %s\n", ESP.getChipModel());
  Serial.printf("Core CPU: %d\n", chip_info.cores);
  Serial.printf("Revisione Silicon: %d\n", chip_info.revision);
  Serial.printf("Frequenza CPU: %d MHz\n", ESP.getCpuFreqMHz());

  // Memoria Flash
  Serial.printf("Dimensione Flash: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("Velocità Flash: %d MHz\n", ESP.getFlashChipSpeed() / 1000000);

  // Memoria RAM interna e PSRAM (se presente sul modulo CAM/MB)
  Serial.printf("RAM Libera (Heap): %d KB\n", ESP.getFreeHeap() / 1024);
  Serial.printf("Dimensione PSRAM: %d MB\n", ESP.getPsramSize() / (1024 * 1024));

  // Versione SDK / Framework
  Serial.printf("Versione ESP-IDF: %s\n", esp_get_idf_version());

  // 1. Lettura del valore numerico grezzo (enum)
  wifi_mode_t currentMode = WiFi.getMode();
  Serial.printf("WiFi Mode : %d\n  %s\n", currentMode, getWiFiModeString(currentMode));
  Serial.printf("WiFi Sleep: DISABILITATO (WiFi.setSleep(false))\n");
  Serial.printf("WiFi set PS: %s, risultato=%d\n",
                wifiPowerSaveConfigured ? "ESEGUITO" : "NON ESEGUITO",
                wifiPowerSaveResult);
  Serial.printf("WiFi inactive time: %s, risultato=%d\n",
                wifiInactiveTimeResult == ESP_OK ? "CONFIGURATO" : "ERRORE",
                wifiInactiveTimeResult);
  Serial.printf("WiFi inactive seconds: %u\n",
                static_cast<unsigned>(AP_INACTIVE_TIMEOUT_SECONDS));

  Serial.println("=========================================\n");
  const char *headers[] = {"X-API-KEY", "X-TIMESTAMP"};
  server.collectHeaders(headers, 2);
  server.on("/status", HTTP_GET, []() {
    status.handle(server);
  });
  server.on("/register", HTTP_POST, []() {
    configuration.handleRegister(server);
  });
  server.on("/led", HTTP_POST, []() {
    if (configuration.authenticate(server)) {
      leds.handle(server);
    }
  });
  server.on("/stream", HTTP_GET, []() {
    streaming.handle(server, configuration);
  });
  server.onNotFound([]() {
    CommunicationUtils::sendError(server, 404, "not found");
  });
  server.begin();

  
}

void loop() {
  server.handleClient();
  internalLed.update();
  leds.update();
}
