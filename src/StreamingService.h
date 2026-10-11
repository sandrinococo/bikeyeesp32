#pragma once

#include <ESPAsyncWebServer.h>
#include <atomic>
#include <esp_camera.h>

class ConfigurationService;

class StreamingService {
 public:
  // Initialize the camera with the configured capture parameters.
  bool beginCamera();
  // Authenticate the request and serve the MJPEG camera stream.
  void handle(AsyncWebServerRequest *request, ConfigurationService &configuration);
  // Return the most recently measured stream frame rate.
  float measuredFps() const;

 private:
  std::atomic<float> fps{0.0f};
  std::atomic<bool> streamActive{false};
};