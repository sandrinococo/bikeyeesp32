#pragma once

#include <ArduinoJson.h>
#include <WebServer.h>

namespace CommunicationUtils {
void sendJson(WebServer &server, int statusCode, JsonDocument &document);
void sendError(WebServer &server, int statusCode, const char *message);
bool parseJsonBody(WebServer &server, JsonDocument &document);
String hmacSha256(const String &key, const String &message);
bool secureEquals(const String &left, const String &right);
String makeSessionToken();
}