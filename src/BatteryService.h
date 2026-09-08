#pragma once

#include <ArduinoJson.h>

class BatteryService {
 public:
  // Configure the ADC and optional charger-status input.
  void begin();
  // Sample the battery and update the moving average.
  void update();
  // Append the current battery values to a JSON status object.
  void writeStatus(JsonObject status) const;
  // Return whether a valid battery measurement is available.
  bool hasMeasurement() const;
  // Return whether the charger input currently indicates charging.
  bool isCharging() const;
  // Return whether the battery has reached the configured full threshold.
  bool isFull() const;
  // Return the estimated battery level from 0 to 100, or -1 if unavailable.
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