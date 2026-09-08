#pragma once

#include <WebServer.h>
#include <esp_camera.h>

class ConfigurationService;

class StreamingService {
 public:
  // Initialize the camera with the configured capture parameters.
  bool beginCamera();
  // Authenticate the request and serve the MJPEG camera stream.
  void handle(WebServer &server, ConfigurationService &configuration);
  // Return the most recently measured stream frame rate.
  float measuredFps() const;

 private:
  float fps = 0.0f;
};