#include "BatteryService.h"

#include <Arduino.h>

#include "config.h"

namespace {
int toPercent(float voltage) {
  const float percentage = (voltage - BATTERY_MIN_MV) * 100.0f /
                          (BATTERY_MAX_MV - BATTERY_MIN_MV);
  return constrain(static_cast<int>(percentage), 0, 100);
}
}

void BatteryService::begin() {
#if BATTERY_ADC_PIN >= 0
  pinMode(BATTERY_ADC_PIN, INPUT);
#endif
#if BATTERY_CHARGE_STATUS_PIN >= 0
  pinMode(BATTERY_CHARGE_STATUS_PIN, BATTERY_CHARGE_STATUS_PULLUP ? INPUT_PULLUP : INPUT);
#endif
  lastSampleMillis = millis() - BATTERY_SAMPLE_INTERVAL_MILLIS;
  update();
}

void BatteryService::update() {
  const uint32_t now = millis();
  if (now - lastSampleMillis < BATTERY_SAMPLE_INTERVAL_MILLIS) {
    return;
  }
  lastSampleMillis = now;

#if BATTERY_ADC_PIN >= 0
  voltageSumMv += analogReadMilliVolts(BATTERY_ADC_PIN) * BATTERY_DIVIDER_RATIO;
  ++sampleCount;
  if (sampleCount >= BATTERY_AVERAGE_SAMPLES) {
    voltageMv = voltageSumMv / sampleCount;
    level = toPercent(voltageMv);
    full = level >= BATTERY_FULL_PERCENT;
    voltageSumMv = 0.0f;
    sampleCount = 0;
  }
#else
  voltageMv = 0.0f;
  level = -1;
  full = false;
#endif

#if BATTERY_CHARGE_STATUS_PIN >= 0
  charging = digitalRead(BATTERY_CHARGE_STATUS_PIN) == BATTERY_CHARGE_STATUS_ACTIVE_LEVEL;
#else
  charging = false;
#endif
}

bool BatteryService::hasMeasurement() const {
  return BATTERY_ADC_PIN >= 0;
}

bool BatteryService::isCharging() const {
  return charging;
}

bool BatteryService::isFull() const {
  return full;
}

int BatteryService::levelPercent() const {
  return level;
}

void BatteryService::writeStatus(JsonObject status) const {
  status["present"] = hasMeasurement();
  status["voltageMv"] = hasMeasurement() ? voltageMv : 0;
  if (hasMeasurement()) {
    status["levelPercent"] = level;
  } else {
    status["levelPercent"] = nullptr;
  }
  status["charging"] = charging;
  status["full"] = full;
  status["sampleIntervalMillis"] = BATTERY_SAMPLE_INTERVAL_MILLIS;
  status["averageSamples"] = BATTERY_AVERAGE_SAMPLES;
  status["samplesCollected"] = sampleCount;
#if BATTERY_CHARGE_STATUS_PIN >= 0
  status["chargeDetection"] = "gpio";
#else
  status["chargeDetection"] = "unavailable";
#endif
}