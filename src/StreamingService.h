#pragma once

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <esp_camera.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <memory>

class ConfigurationService;
struct StreamState;

enum class CameraConfigurationResult {
  Applied,
  Unchanged,
  Unsupported,
  Busy,
  DeinitFailed,
  InitFailed
};

class StreamingService {
 public:
  // Initialize the camera with the configured capture parameters.
  bool beginCamera();
  CameraConfigurationResult configureCamera(framesize_t resolution,
                                             uint8_t framebuffers,
                                             int jpegQuality);
  void writeStatus(JsonObject camera) const;
  // Authenticate the request and serve the MJPEG camera stream.
  void handle(AsyncWebServerRequest *request, ConfigurationService &configuration);
  // Return the most recently measured stream frame rate.
  float measuredFps() const;
  bool isStreamActive() const;
  uint32_t frameErrorCount() const;
  bool hasCapturedFrame() const;
  uint32_t lastFrameMillis() const;

 private:
  void initializeCameraConfig();

  camera_config_t cameraConfig = {};
  SemaphoreHandle_t cameraMutex = nullptr;
  std::weak_ptr<StreamState> activeStreamState;
  std::atomic<bool> cameraReady{false};
  std::atomic<bool> cameraReconfiguring{false};
  std::atomic<float> fps{0.0f};
  std::atomic<bool> streamActive{false};
  std::atomic<uint32_t> frameErrors{0};
  std::atomic<bool> capturedFrame{false};
  std::atomic<uint32_t> lastFrameAt{0};
};