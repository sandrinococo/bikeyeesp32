#include "StatusService.h"

#include <Arduino.h>
#include <WiFi.h>

#include "CommunicationUtils.h"
#include "config.h"

namespace {
const char *frameSizeName(framesize_t frameSize) {
  switch (frameSize) {
    case FRAMESIZE_QQVGA: return "QQVGA";
    case FRAMESIZE_QVGA: return "QVGA";
    case FRAMESIZE_VGA: return "VGA";
    case FRAMESIZE_SVGA: return "SVGA";
    case FRAMESIZE_XGA: return "XGA";
    case FRAMESIZE_SXGA: return "SXGA";
    case FRAMESIZE_UXGA: return "UXGA";
    default: return "unknown";
  }
}

const char *pixelFormatName(pixformat_t pixelFormat) {
  switch (pixelFormat) {
    case PIXFORMAT_JPEG: return "JPEG";
    case PIXFORMAT_RGB565: return "RGB565";
    case PIXFORMAT_YUV422: return "YUV422";
    case PIXFORMAT_GRAYSCALE: return "GRAYSCALE";
    default: return "unknown";
  }
}

int batteryPercent(float voltageMv) {
  const float percentage = (voltageMv - BATTERY_MIN_MV) * 100.0f /
                          (BATTERY_MAX_MV - BATTERY_MIN_MV);
  return constrain(static_cast<int>(percentage), 0, 100);
}
}

StatusService::StatusService(LedService &leds, StreamingService &streaming)
    : leds(leds), streaming(streaming) {}

void StatusService::handle(WebServer &server) const {
  StaticJsonDocument<1024> document;
  sensor_t *sensor = esp_camera_sensor_get();
  document["tipo"] = DEVICE_TYPE;
  document["version"] = DEVICE_VERSION;
  document["deviceName"] = DEVICE_NAME;
  document["serial"] = DEVICE_SERIAL;
  document["running"] = true;
  document["message"] = "Device is running";
  document["ram"]["freeBytes"] = ESP.getFreeHeap();
  document["ram"]["totalBytes"] = ESP.getHeapSize();
  document["wifi"]["mode"] = "accessPoint";
  document["wifi"]["signalDbm"] = WiFi.RSSI();
  document["wifi"]["connectedStations"] = WiFi.softAPgetStationNum();
  document["camera"]["resolution"] = frameSizeName(CAMERA_FRAME_SIZE);
  document["camera"]["format"] = pixelFormatName(PIXFORMAT_JPEG);
  document["camera"]["fps"] = streaming.measuredFps();
  if (sensor != nullptr) {
    JsonObject settings = document["camera"].createNestedObject("settings");
    settings["brightness"] = sensor->status.brightness;
    settings["contrast"] = sensor->status.contrast;
    settings["saturation"] = sensor->status.saturation;
    settings["autoExposure"] = sensor->status.aec;
    settings["exposureLevel"] = sensor->status.ae_level;
  }

  JsonObject battery = document.createNestedObject("battery");
#if BATTERY_ADC_PIN >= 0
  const float voltageMv = analogReadMilliVolts(BATTERY_ADC_PIN) * BATTERY_DIVIDER_RATIO;
  battery["present"] = true;
  battery["voltageMv"] = voltageMv;
  battery["levelPercent"] = batteryPercent(voltageMv);
#else
  battery["present"] = false;
  battery["levelPercent"] = nullptr;
  battery["voltageMv"] = nullptr;
#endif

  JsonObject ledStatus = document.createNestedObject("ledStatus");
  leds.writeStatus(ledStatus);
  CommunicationUtils::sendJson(server, 200, document);
}