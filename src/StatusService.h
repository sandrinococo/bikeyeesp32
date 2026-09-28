#pragma once

#include <WebServer.h>

#include "LedService.h"
#include "StreamingService.h"

class BatteryService;
class InternalLedService;
class OtaService;

class StatusService
{
public:
  // Build the complete device status response and send it as JSON.
  StatusService(LedService &leds, StreamingService &streaming,
                BatteryService &battery, InternalLedService &internalLed,
                OtaService &ota);
  // Handle the unauthenticated GET /status endpoint.
  void handle(WebServer &server) const;

private:
  LedService &leds;
  StreamingService &streaming;
  BatteryService &battery;
  InternalLedService &internalLed;
  OtaService &ota;
};