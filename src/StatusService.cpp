#include "StatusService.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "CommunicationUtils.h"
#include "BatteryService.h"
#include "config.h"
#include "InternalLedService.h"

namespace
{
  const char *resetReasonName(esp_reset_reason_t reason)
  {
    switch (reason)
    {
    case ESP_RST_POWERON:
      return "powerOn";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interruptWatchdog";
    case ESP_RST_TASK_WDT:
      return "taskWatchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deepSleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
    }
  }
}

StatusService::StatusService(LedService &leds, StreamingService &streaming,
                             BatteryService &battery, InternalLedService &internalLed)
    : leds(leds), streaming(streaming), battery(battery), internalLed(internalLed) {}

void StatusService::begin()
{
  bootId.store(esp_random());
}

void StatusService::setAccessPointStarted(bool started)
{
  accessPointStarted.store(started);
}

void StatusService::recordError(const char *code, int detail)
{
  portENTER_CRITICAL(&errorMux);
  snprintf(lastError, sizeof(lastError), "%s", code);
  lastErrorDetail = detail;
  ++errorCount;
  const uint32_t count = errorCount;
  portEXIT_CRITICAL(&errorMux);
  Serial.printf("Firmware error #%lu: %s (detail=%d)\n",
                static_cast<unsigned long>(count), code, detail);
}

void StatusService::recordStationConnected(uint8_t aid)
{
  const uint32_t count = stationConnects.fetch_add(1) + 1;
  Serial.printf("Wi-Fi station connected: aid=%u, total=%lu\n",
                aid, static_cast<unsigned long>(count));
}

void StatusService::recordStationDisconnected(uint8_t aid)
{
  const uint32_t count = stationDisconnects.fetch_add(1) + 1;
  Serial.printf("Wi-Fi station disconnected: aid=%u, total=%lu\n",
                aid, static_cast<unsigned long>(count));
}

void StatusService::handle(AsyncWebServerRequest *request) const
{
  StaticJsonDocument<3072> document;
  char lastErrorSnapshot[sizeof(lastError)];
  int lastErrorDetailSnapshot;
  uint32_t errorCountSnapshot;
  portENTER_CRITICAL(&errorMux);
  snprintf(lastErrorSnapshot, sizeof(lastErrorSnapshot), "%s", lastError);
  lastErrorDetailSnapshot = lastErrorDetail;
  errorCountSnapshot = errorCount;
  portEXIT_CRITICAL(&errorMux);

  document["tipo"] = DEVICE_TYPE;
  document["version"] = DEVICE_VERSION;
  document["buildDateTime"] = BUILD_DATETIME;
  document["deviceName"] = DEVICE_NAME;
  document["serial"] = DEVICE_SERIAL;
  document["running"] = true;
  document["system"]["bootId"] = bootId.load();
  document["system"]["uptimeSeconds"] = millis() / 1000;
  document["system"]["resetReason"] = resetReasonName(esp_reset_reason());
  document["system"]["resetReasonCode"] = static_cast<int>(esp_reset_reason());
  JsonObject cameraStatus = document.createNestedObject("camera");
  streaming.writeStatus(cameraStatus);
  const bool cameraReady = cameraStatus["ready"] | false;
  const bool healthy = accessPointStarted.load() && cameraReady && errorCountSnapshot == 0;
  document["health"] = healthy ? "ready" : "degraded";
  document["message"] = healthy ? "Device is running" : "One or more diagnostics require attention";
  document["diagnostics"]["errorCount"] = errorCountSnapshot;
  document["diagnostics"]["lastError"] =
      lastErrorSnapshot[0] == '\0' ? nullptr : lastErrorSnapshot;
  if (lastErrorSnapshot[0] != '\0')
  {
    document["diagnostics"]["lastErrorDetail"] = lastErrorDetailSnapshot;
  }
  document["ram"]["freeBytes"] = ESP.getFreeHeap();
  document["ram"]["totalBytes"] = ESP.getHeapSize();
  document["ram"]["minimumFreeBytes"] = ESP.getMinFreeHeap();
  document["ram"]["largestFreeBlockBytes"] =
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  document["wifi"]["mode"] = "accessPoint";
  document["wifi"]["signalDbm"] = WiFi.RSSI();
  document["wifi"]["apStarted"] = accessPointStarted.load();
  document["wifi"]["ip"] = WiFi.softAPIP().toString();
  document["wifi"]["connectedStations"] = WiFi.softAPgetStationNum();
  document["wifi"]["stationConnects"] = stationConnects.load();
  document["wifi"]["stationDisconnects"] = stationDisconnects.load();
  JsonObject batteryStatus = document.createNestedObject("battery");
  battery.writeStatus(batteryStatus);
  JsonObject ledStatus = document.createNestedObject("ledStatus");
  leds.writeStatus(ledStatus);
  JsonObject internalLedStatus = document.createNestedObject("internalLedStatus");
  internalLed.writeStatus(internalLedStatus);
  CommunicationUtils::sendJson(request, 200, document);
}