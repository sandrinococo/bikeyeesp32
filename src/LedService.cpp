#include "LedService.h"

#include "CommunicationUtils.h"
#include "config.h"

void LedService::begin(Preferences &storage) {
  preferences = &storage;
  loadSettings();
  if (!available()) {
    return;
  }
  strip.updateLength(LED_STRIP_COUNT);
  strip.updateType(LED_STRIP_COLOR_ORDER + LED_STRIP_SIGNAL_SPEED);
  strip.setPin(LED_STRIP_GPIO);
  strip.begin();
  strip.clear();
  strip.show();
  apply();
}

bool LedService::available() const {
  return LED_STRIP_ENABLED && LED_STRIP_GPIO >= 0 && LED_STRIP_COUNT > 0;
}

bool LedService::validMode(const String &requestedMode) const {
  return requestedMode == "off" || requestedMode == "solid" || requestedMode == "blink";
}

void LedService::loadSettings() {
  if (preferences == nullptr) {
    return;
  }

  const String savedMode = preferences->getString("ledMode", LED_STRIP_MODE);
  const uint32_t savedInterval = preferences->getUInt("ledInterval", LED_STRIP_INTERVAL_MILLIS);
  if (validMode(savedMode) && savedInterval >= 50 && savedInterval <= 60000) {
    mode = savedMode;
    intervalMillis = savedInterval;
    brightness = preferences->getUChar("ledBright", LED_STRIP_BRIGHTNESS);
    red = preferences->getUChar("ledRed", LED_STRIP_RED);
    green = preferences->getUChar("ledGreen", LED_STRIP_GREEN);
    blue = preferences->getUChar("ledBlue", LED_STRIP_BLUE);
  }
  blinkOn = mode == "solid";
  lastToggleMillis = millis();
}

void LedService::saveSettings() {
  if (preferences == nullptr) {
    return;
  }
  preferences->putString("ledMode", mode);
  preferences->putUInt("ledInterval", intervalMillis);
  preferences->putUChar("ledBright", brightness);
  preferences->putUChar("ledRed", red);
  preferences->putUChar("ledGreen", green);
  preferences->putUChar("ledBlue", blue);
}

void LedService::apply() {
  if (!available()) {
    return;
  }

  strip.setBrightness(brightness);
  const uint32_t color = strip.Color(red, green, blue);
  const bool showColor = mode == "solid" || (mode == "blink" && blinkOn);
  for (uint16_t index = 0; index < LED_STRIP_COUNT; ++index) {
    strip.setPixelColor(index, showColor ? color : 0);
  }
  strip.show();
}

bool LedService::updateSettings(JsonDocument &request) {
  const char *requestedMode = request["mode"] | "off";
  const int requestedBrightness = request["brightness"] | brightness;
  const uint32_t requestedInterval = request["intervalMillis"] | intervalMillis;
  const int requestedRed = request["red"] | red;
  const int requestedGreen = request["green"] | green;
  const int requestedBlue = request["blue"] | blue;
  if (!validMode(requestedMode) || requestedBrightness < 0 || requestedBrightness > 255 ||
      requestedInterval < 50 || requestedInterval > 60000 || requestedRed < 0 ||
      requestedRed > 255 || requestedGreen < 0 || requestedGreen > 255 ||
      requestedBlue < 0 || requestedBlue > 255) {
    return false;
  }

  mode = requestedMode;
  brightness = requestedBrightness;
  intervalMillis = requestedInterval;
  red = requestedRed;
  green = requestedGreen;
  blue = requestedBlue;
  blinkOn = mode == "solid";
  lastToggleMillis = millis();
  saveSettings();
  apply();
  return true;
}

void LedService::handle(WebServer &server) {
  if (!available()) {
    CommunicationUtils::sendError(server, 409, "led strip is not configured");
    return;
  }

  StaticJsonDocument<256> request;
  if (!CommunicationUtils::parseJsonBody(server, request)) {
    CommunicationUtils::sendError(server, 400, "invalid json");
    return;
  }
  if (!updateSettings(request)) {
    CommunicationUtils::sendError(
        server, 400,
        "mode must be off, solid or blink; intervalMillis must be 50..60000; color values must be 0..255");
    return;
  }

  StaticJsonDocument<256> response;
  response["updated"] = true;
  response["mode"] = mode;
  response["brightness"] = brightness;
  response["red"] = red;
  response["green"] = green;
  response["blue"] = blue;
  response["intervalMillis"] = intervalMillis;
  CommunicationUtils::sendJson(server, 200, response);
}

void LedService::writeStatus(JsonObject status) const {
  status["present"] = available();
  status["configured"] = LED_STRIP_ENABLED != 0;
  status["gpio"] = LED_STRIP_GPIO;
  status["count"] = LED_STRIP_COUNT;
  status["brightness"] = brightness;
  status["red"] = red;
  status["green"] = green;
  status["blue"] = blue;
  status["mode"] = mode;
  status["intervalMillis"] = intervalMillis;
  status["blinkOn"] = blinkOn;
}

void LedService::update() {
  if (!available() || mode != "blink") {
    return;
  }
  const uint32_t now = millis();
  if (now - lastToggleMillis < intervalMillis) {
    return;
  }
  lastToggleMillis = now;
  blinkOn = !blinkOn;
  apply();
}