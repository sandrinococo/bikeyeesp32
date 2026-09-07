#pragma once

#include <ArduinoJson.h>

class BatteryService;

class InternalLedService {
 public:
  void begin(BatteryService &battery);
  void update();
  void writeStatus(JsonObject status) const;

 private:
  enum class State { Off, LowBattery, Charging, Full };
  State state() const;
  void apply(bool on);

  BatteryService *battery = nullptr;
  State currentState = State::Off;
  bool isOn = false;
  uint32_t lastToggleMillis = 0;
};