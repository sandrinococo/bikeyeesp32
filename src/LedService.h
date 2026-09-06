#pragma once

#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>

#include "config.h"

class LedService {
 public:
  void begin(Preferences &preferences);
  void update();
  void handle(WebServer &server);
  void writeStatus(JsonObject status) const;

 private:
  bool available() const;
  bool validMode(const String &mode) const;
  void loadSettings();
  void saveSettings();
  void apply();
  bool updateSettings(JsonDocument &request);

  Preferences *preferences = nullptr;
  Adafruit_NeoPixel strip;
  String mode = LED_STRIP_MODE;
  uint8_t brightness = LED_STRIP_BRIGHTNESS;
  uint8_t red = LED_STRIP_RED;
  uint8_t green = LED_STRIP_GREEN;
  uint8_t blue = LED_STRIP_BLUE;
  uint32_t intervalMillis = LED_STRIP_INTERVAL_MILLIS;
  bool blinkOn = false;
  uint32_t lastToggleMillis = 0;
};