#pragma once

#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>

#include "config.h"

class LedService {
 public:
  // Initialize the LED strip and load settings from non-volatile storage.
  void begin(Preferences &preferences);
  // Advance the blink state when the configured interval expires.
  void update();
  // Validate and apply a JSON LED control request.
  void handle(WebServer &server);
  // Append the current LED strip settings to a JSON status object.
  void writeStatus(JsonObject status) const;

 private:
  // Return whether the configured LED strip can be used.
  bool available() const;
  // Return whether a requested mode is supported.
  bool validMode(const String &mode) const;
  // Load LED settings from non-volatile storage.
  void loadSettings();
  // Save the current LED settings to non-volatile storage.
  void saveSettings();
  // Apply the current settings to the physical LED strip.
  void apply();
  // Update settings from a parsed JSON request.
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