#include "CommunicationUtils.h"

#include <esp_system.h>
#include <mbedtls/md.h>

namespace {
String toHex(const uint8_t *bytes, size_t length) {
  const char hex[] = "0123456789abcdef";
  String result;
  result.reserve(length * 2);
  for (size_t index = 0; index < length; ++index) {
    result += hex[(bytes[index] >> 4) & 0x0f];
    result += hex[bytes[index] & 0x0f];
  }
  return result;
}
}

namespace CommunicationUtils {
void sendJson(WebServer &server, int statusCode, JsonDocument &document) {
  String payload;
  serializeJson(document, payload);
  server.send(statusCode, "application/json", payload);
}

void sendError(WebServer &server, int statusCode, const char *message) {
  StaticJsonDocument<192> document;
  document["error"] = message;
  sendJson(server, statusCode, document);
}

bool parseJsonBody(WebServer &server, JsonDocument &document) {
  return !deserializeJson(document, server.arg("plain"));
}

String hmacSha256(const String &key, const String &message) {
  uint8_t digest[32];
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info,
                  reinterpret_cast<const unsigned char *>(key.c_str()), key.length(),
                  reinterpret_cast<const unsigned char *>(message.c_str()), message.length(),
                  digest);
  return toHex(digest, sizeof(digest));
}

bool secureEquals(const String &left, const String &right) {
  if (left.length() != right.length()) {
    return false;
  }
  uint8_t difference = 0;
  for (size_t index = 0; index < left.length(); ++index) {
    difference |= static_cast<uint8_t>(left[index] ^ right[index]);
  }
  return difference == 0;
}

String makeSessionToken() {
  uint8_t randomBytes[32];
  for (size_t index = 0; index < sizeof(randomBytes); index += 4) {
    uint32_t value = esp_random();
    memcpy(randomBytes + index, &value, 4);
  }
  return toHex(randomBytes, sizeof(randomBytes));
}
}