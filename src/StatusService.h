#pragma once

#include <WebServer.h>

#include "LedService.h"
#include "StreamingService.h"

class StatusService {
 public:
  StatusService(LedService &leds, StreamingService &streaming);
  void handle(WebServer &server) const;

 private:
  LedService &leds;
  StreamingService &streaming;
};