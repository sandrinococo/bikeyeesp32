#pragma once

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

namespace CommunicationUtils {
// Serialize a JSON document and send it as an HTTP response.
void sendJson(AsyncWebServerRequest *request, int statusCode, JsonDocument &document);
// Send a JSON error response with the supplied HTTP status and message.
void sendError(AsyncWebServerRequest *request, int statusCode, const char *message);
// Calculate the hexadecimal HMAC-SHA256 digest for a message.
String hmacSha256(const String &key, const String &message);
// Compare two strings without exposing timing information through early exit.
bool secureEquals(const String &left, const String &right);
// Generate a cryptographically random session token.
String makeSessionToken();
}