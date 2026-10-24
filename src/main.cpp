#include <Arduino.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "CommunicationUtils.h"
#include "BatteryService.h"
#include "ConfigurationService.h"
#include "InternalLedService.h"
#include "LedService.h"
#include "StatusService.h"
#include "StreamingService.h"
#include "config.h"

const char BUILD_DATETIME[] = __DATE__ " " __TIME__;

AsyncWebServer server(80);
ConfigurationService configuration;
LedService leds;
BatteryService battery;
InternalLedService internalLed;
StreamingService streaming;
StatusService status(leds, streaming, battery, internalLed);


// Funzione di supporto per convertire il valore numerico della modalità in testo leggibile
const char* getWiFiModeString(wifi_mode_t mode) {
  switch (mode) {
    case WIFI_OFF:     return "WIFI_OFF (Spento)";
    case WIFI_STA:     return "WIFI_STA (Station / Client)";
    case WIFI_AP:      return "WIFI_AP (Access Point)";
    case WIFI_AP_STA:  return "WIFI_AP_STA (Access Point + Station)";
    default:           return "SCONOSCIUTO";
  }
}


void setup() {
  Serial.begin(115200);
  status.begin();
  Serial.printf("Build date/time: %s\n", BUILD_DATETIME);
  Serial.printf("Reset reason code: %d (text available from GET /status)\n",
                static_cast<int>(esp_reset_reason()));
  configuration.begin();

  battery.begin();
  internalLed.begin(battery);
  leds.begin(configuration.storage());
  if (!streaming.beginCamera()) {
    status.recordError("camera_init_failed");
    Serial.println("Camera initialization failed");
    while (true) {
      delay(1000);
    }
  }

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
      status.recordStationConnected(info.wifi_ap_staconnected.aid);
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
      status.recordStationDisconnected(info.wifi_ap_stadisconnected.aid);
    }
  });
  const bool accessPointStarted =
      WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  status.setAccessPointStarted(accessPointStarted);
  if (!accessPointStarted) {
    status.recordError("wifi_ap_start_failed");
  }
  esp_err_t wifiPowerSaveResult = ESP_OK;
  bool wifiPowerSaveConfigured = false;
#if WIFI_DISABLE_POWER_SAVE
  wifiPowerSaveResult = esp_wifi_set_ps(WIFI_PS_NONE);
  wifiPowerSaveConfigured = true;
#endif
  esp_err_t wifiInactiveTimeResult =
      esp_wifi_set_inactive_time(WIFI_IF_AP, AP_INACTIVE_TIMEOUT_SECONDS);
  if (wifiPowerSaveConfigured && wifiPowerSaveResult != ESP_OK) {
    status.recordError("wifi_power_save_config_failed", wifiPowerSaveResult);
  }
  if (wifiInactiveTimeResult != ESP_OK) {
    status.recordError("wifi_inactive_time_config_failed", wifiInactiveTimeResult);
  }
  Serial.println(WiFi.softAPIP());

  Serial.println("\n=== CONFIGURAZIONE E SPECIFICHE ESP32 ===");
  
  // Modello e Revisione del Chip
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  Serial.printf("Modello Chip: %s\n", ESP.getChipModel());
  Serial.printf("Core CPU: %d\n", chip_info.cores);
  Serial.printf("Revisione Silicon: %d\n", chip_info.revision);
  Serial.printf("Frequenza CPU: %d MHz\n", ESP.getCpuFreqMHz());

  // Memoria Flash
  Serial.printf("Dimensione Flash: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("Velocità Flash: %d MHz\n", ESP.getFlashChipSpeed() / 1000000);

  // Memoria RAM interna e PSRAM (se presente sul modulo CAM/MB)
  Serial.printf("RAM Libera (Heap): %d KB\n", ESP.getFreeHeap() / 1024);
  Serial.printf("Dimensione PSRAM: %d MB\n", ESP.getPsramSize() / (1024 * 1024));

  // Versione SDK / Framework
  Serial.printf("Versione ESP-IDF: %s\n", esp_get_idf_version());

  // 1. Lettura del valore numerico grezzo (enum)
  wifi_mode_t currentMode = WiFi.getMode();
  Serial.printf("WiFi Mode : %d\n  %s\n", currentMode, getWiFiModeString(currentMode));
  Serial.printf("WiFi Sleep: DISABILITATO (WiFi.setSleep(false))\n");
  Serial.printf("WiFi set PS: %s, risultato=%d\n",
                wifiPowerSaveConfigured ? "ESEGUITO" : "NON ESEGUITO",
                wifiPowerSaveResult);
  Serial.printf("WiFi inactive time: %s, risultato=%d\n",
                wifiInactiveTimeResult == ESP_OK ? "CONFIGURATO" : "ERRORE",
                wifiInactiveTimeResult);
  Serial.printf("WiFi inactive seconds: %u\n",
                static_cast<unsigned>(AP_INACTIVE_TIMEOUT_SECONDS));

  Serial.println("=========================================\n");
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    status.handle(request);
  });
  auto *registerHandler = new AsyncCallbackJsonWebHandler(
      "/register", [](AsyncWebServerRequest *request, JsonVariant &body) {
        configuration.handleRegister(request, body.as<JsonVariantConst>());
      });
  registerHandler->setMethod(HTTP_POST);
  registerHandler->setMaxContentLength(384);
  server.addHandler(registerHandler);
  auto *ledHandler = new AsyncCallbackJsonWebHandler(
      "/led", [](AsyncWebServerRequest *request, JsonVariant &body) {
        if (configuration.authenticate(request)) {
          leds.handle(request, body.as<JsonVariantConst>());
        }
      });
  ledHandler->setMethod(HTTP_POST);
  ledHandler->setMaxContentLength(384);
  server.addHandler(ledHandler);
  auto *cameraHandler = new AsyncCallbackJsonWebHandler(
      "/camera", [](AsyncWebServerRequest *request, JsonVariant &body) {
        if (!configuration.authenticate(request)) {
          return;
        }
        if (!body.is<JsonObjectConst>()) {
          CommunicationUtils::sendError(request, 400, "JSON object required");
          return;
        }

        JsonObjectConst settings = body.as<JsonObjectConst>();
        if (!settings["resolution"].is<const char *>() ||
            !settings["framebuffers"].is<int>() ||
            !settings["jpegquality"].is<int>()) {
          CommunicationUtils::sendError(
              request, 400, "resolution, framebuffers and jpegquality are required");
          return;
        }

        const char *resolutionName = settings["resolution"];
        framesize_t resolution;
        if (strcmp(resolutionName, "QQVGA") == 0) {
          resolution = FRAMESIZE_QQVGA;
        } else if (strcmp(resolutionName, "QVGA") == 0) {
          resolution = FRAMESIZE_QVGA;
        } else if (strcmp(resolutionName, "VGA") == 0) {
          resolution = FRAMESIZE_VGA;
        } else if (strcmp(resolutionName, "SVGA") == 0) {
          resolution = FRAMESIZE_SVGA;
        } else if (strcmp(resolutionName, "XGA") == 0) {
          resolution = FRAMESIZE_XGA;
        } else if (strcmp(resolutionName, "SXGA") == 0) {
          resolution = FRAMESIZE_SXGA;
        } else if (strcmp(resolutionName, "UXGA") == 0) {
          resolution = FRAMESIZE_UXGA;
        } else {
          CommunicationUtils::sendError(request, 400, "unsupported resolution");
          return;
        }

        const int framebuffers = settings["framebuffers"];
        const int jpegQuality = settings["jpegquality"];
        if (framebuffers < 1 || framebuffers > 4 ||
            jpegQuality < 12 || jpegQuality > 20) {
          CommunicationUtils::sendError(
              request, 400, "framebuffers must be 1..4 and jpegquality 12..20");
          return;
        }

        const CameraConfigurationResult result = streaming.configureCamera(
            resolution, static_cast<uint8_t>(framebuffers), jpegQuality);
        if (result == CameraConfigurationResult::Unsupported) {
          CommunicationUtils::sendError(
              request, 409, "requested framebuffers require camera PSRAM");
          return;
        }
        if (result == CameraConfigurationResult::Busy) {
          CommunicationUtils::sendError(request, 409, "camera reconfiguration is in progress");
          return;
        }
        if (result == CameraConfigurationResult::DeinitFailed ||
            result == CameraConfigurationResult::InitFailed) {
          status.recordError("camera_reconfigure_failed", static_cast<int>(result));
          CommunicationUtils::sendError(request, 500, "camera reconfiguration failed");
          return;
        }

        StaticJsonDocument<256> response;
        response["configured"] = true;
        response["changed"] = result == CameraConfigurationResult::Applied;
        JsonObject camera = response.createNestedObject("camera");
        camera["resolution"] = resolutionName;
        camera["framebuffers"] = framebuffers;
        camera["jpegquality"] = jpegQuality;
        CommunicationUtils::sendJson(request, 200, response);
      });
  cameraHandler->setMethod(HTTP_POST);
  cameraHandler->setMaxContentLength(192);
  server.addHandler(cameraHandler);
  server.on("/stream", HTTP_GET, [](AsyncWebServerRequest *request) {
    streaming.handle(request, configuration);
  });
  server.onNotFound([](AsyncWebServerRequest *request) {
    CommunicationUtils::sendError(request, 404, "not found");
  });
  server.begin();

  
}

void loop() {
  internalLed.update();
  leds.update();
}
