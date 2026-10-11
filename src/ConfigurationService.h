#pragma once

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

class ConfigurationService {
 public:
  // Load authentication data and clock offset from non-volatile storage.
  void begin();
  // Authenticate the request using the session token and timestamp headers.
  bool authenticate(AsyncWebServerRequest *request);
  // Validate a registration request and create a new application session.
  void handleRegister(AsyncWebServerRequest *request, JsonVariantConst body);
  // Return the persistent storage used by other device services.
  Preferences &storage();

 private:
  // Return the current device time corrected to Unix time.
  int64_t deviceUnixTime() const;

  Preferences preferences;
  String sessionToken;
  int64_t clockOffsetSeconds = 0;
};