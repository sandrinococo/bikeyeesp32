#pragma once

#include <ArduinoJson.h>

class BatteryService;

class InternalLedService {
 public:
  // Connect the service to the battery status provider.
  void begin(BatteryService &battery);
  // Update the LED state according to battery and charger conditions.
  void update();
  // Append the internal LED state to a JSON status object.
  void writeStatus(JsonObject status) const;

 private:
  // Visual states used to report battery conditions through the LED.
  enum class State { Off, LowBattery, Charging, Full };
  // Select the state that corresponds to the current battery condition.
  State state() const;
  // Apply the requested logical state to the physical GPIO.
  void apply(bool on);

  BatteryService *battery = nullptr;
  State currentState = State::Off;
  bool isOn = false;
  uint32_t lastToggleMillis = 0;
};