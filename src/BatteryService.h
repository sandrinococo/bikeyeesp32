#pragma once

#include <ArduinoJson.h>

class BatteryService {
 public:
  void begin();
  void update();
  void writeStatus(JsonObject status) const;
  bool hasMeasurement() const;
  bool isCharging() const;
  bool isFull() const;
  int levelPercent() const;

 private:
  float voltageMv = 0.0f;
  int level = -1;
  bool charging = false;
  bool full = false;
  float voltageSumMv = 0.0f;
  uint16_t sampleCount = 0;
  uint32_t lastSampleMillis = 0;
};