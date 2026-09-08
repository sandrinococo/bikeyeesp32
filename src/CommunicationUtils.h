#pragma once

#include <ArduinoJson.h>
#include <WebServer.h>

namespace CommunicationUtils {
// Serialize a JSON document and send it as an HTTP response.
void sendJson(WebServer &server, int statusCode, JsonDocument &document);
// Send a JSON error response with the supplied HTTP status and message.
void sendError(WebServer &server, int statusCode, const char *message);
// Parse the request body into a JSON document.
bool parseJsonBody(WebServer &server, JsonDocument &document);
// Calculate the hexadecimal HMAC-SHA256 digest for a message.
String hmacSha256(const String &key, const String &message);
// Compare two strings without exposing timing information through early exit.
bool secureEquals(const String &left, const String &right);
// Generate a cryptographically random session token.
String makeSessionToken();
}