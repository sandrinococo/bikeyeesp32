#include "InternalLedService.h"

#include <Arduino.h>

#include "BatteryService.h"
#include "config.h"

void InternalLedService::begin(BatteryService &batteryService) {
  battery = &batteryService;
#if INTERNAL_LED_GPIO >= 0
  pinMode(INTERNAL_LED_GPIO, OUTPUT);
#endif
  currentState = state();
  lastToggleMillis = millis();
  apply(currentState == State::Full);
}

InternalLedService::State InternalLedService::state() const {
  if (battery == nullptr) {
    return State::Off;
  }
  if (battery->isCharging()) {
    return battery->hasMeasurement() && battery->isFull() ? State::Full : State::Charging;
  }
  if (!battery->hasMeasurement()) {
    return State::Off;
  }
  return battery->levelPercent() >= 0 && battery->levelPercent() < BATTERY_LOW_PERCENT
             ? State::LowBattery
             : State::Off;
}

void InternalLedService::apply(bool on) {
#if INTERNAL_LED_GPIO >= 0
  digitalWrite(INTERNAL_LED_GPIO, on == (INTERNAL_LED_ACTIVE_LEVEL != 0) ? HIGH : LOW);
#else
  (void)on;
#endif
  isOn = on;
}

void InternalLedService::update() {
  if (battery == nullptr) {
    return;
  }

  battery->update();
  const State nextState = state();
  if (nextState != currentState) {
    currentState = nextState;
    lastToggleMillis = millis();
    apply(currentState == State::Full);
  }

  uint32_t interval = 0;
  if (currentState == State::LowBattery) {
    interval = INTERNAL_LED_LOW_BATTERY_INTERVAL_MILLIS;
  } else if (currentState == State::Charging) {
    interval = INTERNAL_LED_CHARGING_INTERVAL_MILLIS;
  } else {
    return;
  }

  const uint32_t now = millis();
  if (now - lastToggleMillis >= interval) {
    lastToggleMillis = now;
    apply(!isOn);
  }
}

void InternalLedService::writeStatus(JsonObject status) const {
  const char *stateName = "off";
  if (currentState == State::LowBattery) {
    stateName = "lowBattery";
  } else if (currentState == State::Charging) {
    stateName = "charging";
  } else if (currentState == State::Full) {
    stateName = "full";
  }
  status["present"] = INTERNAL_LED_GPIO >= 0;
  status["colorable"] = false;
  status["gpio"] = INTERNAL_LED_GPIO;
  status["state"] = stateName;
  status["on"] = isOn;
  status["lowBatteryIntervalMillis"] = INTERNAL_LED_LOW_BATTERY_INTERVAL_MILLIS;
  status["chargingIntervalMillis"] = INTERNAL_LED_CHARGING_INTERVAL_MILLIS;
}