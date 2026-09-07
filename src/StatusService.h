#pragma once

#include <WebServer.h>

#include "LedService.h"
#include "StreamingService.h"

class BatteryService;
class InternalLedService;

class StatusService {
 public:
  StatusService(LedService &leds, StreamingService &streaming,
                BatteryService &battery, InternalLedService &internalLed);
  void handle(WebServer &server) const;

 private:
  LedService &leds;
  StreamingService &streaming;
  BatteryService &battery;
  InternalLedService &internalLed;
};