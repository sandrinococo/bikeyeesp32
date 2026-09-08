#pragma once

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>

class ConfigurationService {
 public:
  // Load authentication data and clock offset from non-volatile storage.
  void begin();
  // Authenticate the request using the session token and timestamp headers.
  bool authenticate(WebServer &server);
  // Validate a registration request and create a new application session.
  void handleRegister(WebServer &server);
  // Return the persistent storage used by other device services.
  Preferences &storage();

 private:
  // Return the current device time corrected to Unix time.
  int64_t deviceUnixTime() const;

  Preferences preferences;
  String sessionToken;
  int64_t clockOffsetSeconds = 0;
};