#pragma once

#include <ArduinoJson.h>

#include "config.h"

class OtaService
{
public:
    // Start the Arduino OTA listener on the active Wi-Fi interface.
    void begin();
    // Serve pending OTA requests; must be called from the main loop.
    void update();
    // Append the current OTA state to a JSON status object.
    void writeStatus(JsonObject status) const;

private:
    bool started = false;
    bool inProgress = false;
    uint8_t progressPercent = 0;
};
