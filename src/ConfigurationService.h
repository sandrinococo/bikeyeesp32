#pragma once

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>

class ConfigurationService {
 public:
  void begin();
  bool authenticate(WebServer &server);
  void handleRegister(WebServer &server);
  Preferences &storage();

 private:
  int64_t deviceUnixTime() const;

  Preferences preferences;
  String sessionToken;
  int64_t clockOffsetSeconds = 0;
};