#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "CommunicationUtils.h"
#include "ConfigurationService.h"
#include "LedService.h"
#include "StatusService.h"
#include "StreamingService.h"
#include "config.h"

WebServer server(80);
ConfigurationService configuration;
LedService leds;
StreamingService streaming;
StatusService status(leds, streaming);

void setup() {
  Serial.begin(115200);
  configuration.begin();

  leds.begin(configuration.storage());
  if (!streaming.beginCamera()) {
    Serial.println("Camera initialization failed");
    while (true) {
      delay(1000);
    }
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, 1);
  Serial.print("AP address: ");
  Serial.println(WiFi.softAPIP());

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
  leds.update();
}
