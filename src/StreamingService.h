#pragma once

#include <WebServer.h>
#include <esp_camera.h>

class ConfigurationService;

class StreamingService {
 public:
  bool beginCamera();
  void handle(WebServer &server, ConfigurationService &configuration);
  float measuredFps() const;

 private:
  float fps = 0.0f;
};