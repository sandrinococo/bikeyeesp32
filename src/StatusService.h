#pragma once

#include <ESPAsyncWebServer.h>
#include <atomic>
#include <stdint.h>

#include "LedService.h"
#include "StreamingService.h"

class BatteryService;
class InternalLedService;

class StatusService {
 public:
  // Build the complete device status response and send it as JSON.
  StatusService(LedService &leds, StreamingService &streaming,
                BatteryService &battery, InternalLedService &internalLed);
  // Handle the unauthenticated GET /status endpoint.
  void handle(AsyncWebServerRequest *request) const;
  void begin();
  void setAccessPointStarted(bool started);
  void recordError(const char *code, int detail = 0);
  void recordStationConnected(uint8_t aid);
  void recordStationDisconnected(uint8_t aid);

 private:
  LedService &leds;
  StreamingService &streaming;
  BatteryService &battery;
  InternalLedService &internalLed;
  std::atomic<uint32_t> bootId{0};
  std::atomic<bool> accessPointStarted{false};
  std::atomic<uint32_t> stationConnects{0};
  std::atomic<uint32_t> stationDisconnects{0};
  mutable portMUX_TYPE errorMux = portMUX_INITIALIZER_UNLOCKED;
  char lastError[40] = "";
  int lastErrorDetail = 0;
  uint32_t errorCount = 0;
};