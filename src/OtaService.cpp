#include "OtaService.h"

#include <Arduino.h>
#include <WiFi.h>

#if OTA_ENABLED
#include <ArduinoOTA.h>
#include <esp_camera.h>
#endif

void OtaService::begin()
{
#if OTA_ENABLED
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setRebootOnSuccess(true);

    ArduinoOTA.onStart([this]()
                       {
    inProgress = true;
    progressPercent = 0;
    // Release the camera driver so the DMA buffers do not clash with the flash write.
    esp_camera_deinit();
    Serial.println("OTA: update started"); });
    ArduinoOTA.onProgress([this](unsigned int done, unsigned int total)
                          {
    progressPercent = total > 0 ? static_cast<uint8_t>((done * 100ULL) / total) : 0;
    Serial.printf("OTA: %u%%\r", progressPercent); });
    ArduinoOTA.onEnd([this]()
                     {
    inProgress = false;
    progressPercent = 100;
    Serial.println("\nOTA: update completed, rebooting"); });
    ArduinoOTA.onError([this](ota_error_t error)
                       {
    inProgress = false;
    Serial.printf("OTA: error %u\n", error);
    ESP.restart(); });

    ArduinoOTA.begin();
    started = true;
    Serial.printf("OTA: listening on %s:%u (hostname %s)\n",
                  WiFi.softAPIP().toString().c_str(),
                  static_cast<unsigned>(OTA_PORT), OTA_HOSTNAME);
#endif
}

void OtaService::update()
{
#if OTA_ENABLED
    if (!started)
    {
        return;
    }
    ArduinoOTA.handle();
#endif
}

void OtaService::writeStatus(JsonObject status) const
{
    status["enabled"] = OTA_ENABLED != 0;
    status["listening"] = started;
    status["hostname"] = OTA_HOSTNAME;
    status["port"] = OTA_PORT;
    status["updating"] = inProgress;
    status["progressPercent"] = progressPercent;
}
