#include "ConfigurationService.h"

#include <Arduino.h>

#include "CommunicationUtils.h"
#include "config.h"

void ConfigurationService::begin() {
  preferences.begin("device-auth", false);
  sessionToken = preferences.getString("session", "");
  clockOffsetSeconds = preferences.getLong64("clockOffset", 0);
}

Preferences &ConfigurationService::storage() {
  return preferences;
}

int64_t ConfigurationService::deviceUnixTime() const {
  return static_cast<int64_t>(millis() / 1000ULL) + clockOffsetSeconds;
}

bool ConfigurationService::authenticate(WebServer &server) {
  if (sessionToken.isEmpty()) {
    CommunicationUtils::sendError(server, 401, "device is not registered");
    return false;
  }

  const String timestampHeader = server.header("X-TIMESTAMP");
  const String apiKey = server.header("X-API-KEY");
  if (timestampHeader.isEmpty() || apiKey.isEmpty()) {
    CommunicationUtils::sendError(server, 401, "missing authentication headers");
    return false;
  }

  int64_t requestTime = strtoll(timestampHeader.c_str(), nullptr, 10);
  int64_t difference = deviceUnixTime() - requestTime;
  if (difference < 0) {
    difference = -difference;
  }
  if (difference > AUTH_WINDOW_SECONDS) {
    CommunicationUtils::sendError(server, 401, "timestamp expired");
    return false;
  }

  const String expected = CommunicationUtils::hmacSha256(
      sessionToken, sessionToken + ":" + timestampHeader);
  if (!CommunicationUtils::secureEquals(expected, apiKey)) {
    CommunicationUtils::sendError(server, 401, "invalid api key");
    return false;
  }
  return true;
}

void ConfigurationService::handleRegister(WebServer &server) {
  StaticJsonDocument<384> request;
  if (!CommunicationUtils::parseJsonBody(server, request)) {
    CommunicationUtils::sendError(server, 400, "invalid json");
    return;
  }

  const char *pin = request["pin"] | "";
  const char *nonce = request["nonce"] | "";
  const char *proof = request["proof"] | "";
  int64_t requestTime = request["timestamp"] | 0LL;
  if (strlen(pin) == 0 || strlen(nonce) < 16 || strlen(proof) == 0 || requestTime <= 0) {
    CommunicationUtils::sendError(server, 400, "pin, nonce, proof and timestamp are required");
    return;
  }

  if (strcmp(pin, DEVICE_PIN) != 0 ||
      !CommunicationUtils::secureEquals(CommunicationUtils::hmacSha256(DEVICE_PIN, nonce), proof)) {
    CommunicationUtils::sendError(server, 401, "invalid registration proof");
    return;
  }

  sessionToken = CommunicationUtils::makeSessionToken();
  clockOffsetSeconds = requestTime - static_cast<int64_t>(millis() / 1000ULL);
  preferences.putString("session", sessionToken);
  preferences.putLong64("clockOffset", clockOffsetSeconds);

  StaticJsonDocument<256> response;
  response["registered"] = true;
  response["sessionToken"] = sessionToken;
  response["timestamp"] = requestTime;
  CommunicationUtils::sendJson(server, 200, response);
}