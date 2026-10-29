#pragma once

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <esp_camera.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <memory>

class ConfigurationService;
struct StreamState;
struct CapturedFrame;

enum class CameraConfigurationResult
{
  Applied,
  Unchanged,
  Unsupported,
  Busy,
  DeinitFailed,
  InitFailed
};

class StreamingService
{
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
  void captureLoop();
  void cacheSensorSettings();

  camera_config_t cameraConfig = {};
  SemaphoreHandle_t cameraMutex = nullptr;
  SemaphoreHandle_t frameMutex = nullptr;
  std::shared_ptr<CapturedFrame> latestFrame;
  std::weak_ptr<StreamState> activeStreamState;
  int brightness = 0;
  int contrast = 0;
  int saturation = 0;
  int autoExposure = 0;
  int exposureLevel = 0;
  bool hasSensorSettings = false;
  std::atomic<bool> cameraReady{false};
  std::atomic<bool> cameraReconfiguring{false};
  std::atomic<float> fps{0.0f};
  std::atomic<float> captureFps{0.0f};
  std::atomic<uint32_t> captureDurationUs{0};
  std::atomic<uint32_t> frameSendMs{0};
  std::atomic<uint32_t> frameSendAgeMs{0};
  std::atomic<bool> streamActive{false};
  std::atomic<uint32_t> frameErrors{0};
  std::atomic<bool> capturedFrame{false};
  std::atomic<uint32_t> lastFrameAt{0};
};