#include "StreamingService.h"

#include <Arduino.h>
#include <algorithm>
#include <functional>
#include <memory>

#include "CommunicationUtils.h"
#include "ConfigurationService.h"
#include "config.h"

// AI Thinker ESP32-CAM pin map.
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

struct StreamState {
  camera_fb_t *frame = nullptr;
  char header[96] = {};
  size_t headerLength = 0;
  size_t headerOffset = 0;
  size_t frameOffset = 0;
  size_t footerOffset = 0;
  uint32_t frameCount = 0;
  uint32_t startMillis = millis();
  std::function<void()> onDestroy;
  SemaphoreHandle_t cameraMutex = nullptr;

  ~StreamState() {
    if (cameraMutex != nullptr && xSemaphoreTake(cameraMutex, portMAX_DELAY) == pdTRUE) {
      if (frame != nullptr) {
        esp_camera_fb_return(frame);
        frame = nullptr;
      }
      xSemaphoreGive(cameraMutex);
    }
    if (onDestroy) {
      onDestroy();
    }
  }
};

bool StreamingService::beginCamera() {
  cameraMutex = xSemaphoreCreateMutex();
  if (cameraMutex == nullptr) {
    Serial.println("Camera mutex initialization failed");
    return false;
  }
  initializeCameraConfig();
  if (xSemaphoreTake(cameraMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  const bool initialized = esp_camera_init(&cameraConfig) == ESP_OK;
  cameraReady.store(initialized);
  xSemaphoreGive(cameraMutex);
  return initialized;
}

void StreamingService::initializeCameraConfig() {
  cameraConfig = {};
  cameraConfig.ledc_channel = LEDC_CHANNEL_0;
  cameraConfig.ledc_timer = LEDC_TIMER_0;
  cameraConfig.pin_d0 = Y2_GPIO_NUM;
  cameraConfig.pin_d1 = Y3_GPIO_NUM;
  cameraConfig.pin_d2 = Y4_GPIO_NUM;
  cameraConfig.pin_d3 = Y5_GPIO_NUM;
  cameraConfig.pin_d4 = Y6_GPIO_NUM;
  cameraConfig.pin_d5 = Y7_GPIO_NUM;
  cameraConfig.pin_d6 = Y8_GPIO_NUM;
  cameraConfig.pin_d7 = Y9_GPIO_NUM;
  cameraConfig.pin_xclk = XCLK_GPIO_NUM;
  cameraConfig.pin_pclk = PCLK_GPIO_NUM;
  cameraConfig.pin_vsync = VSYNC_GPIO_NUM;
  cameraConfig.pin_href = HREF_GPIO_NUM;
  cameraConfig.pin_sccb_sda = SIOD_GPIO_NUM;
  cameraConfig.pin_sccb_scl = SIOC_GPIO_NUM;
  cameraConfig.pin_pwdn = PWDN_GPIO_NUM;
  cameraConfig.pin_reset = RESET_GPIO_NUM;
  cameraConfig.xclk_freq_hz = 20000000;
  cameraConfig.pixel_format = PIXFORMAT_JPEG;
  cameraConfig.frame_size = CAMERA_FRAME_SIZE;
  cameraConfig.jpeg_quality = CAMERA_JPEG_QUALITY;
  cameraConfig.fb_count = psramFound() ? CAMERA_FB_COUNT : 1;
  cameraConfig.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    cameraConfig.fb_count = 1;
    cameraConfig.fb_location = CAMERA_FB_IN_DRAM;
  }
}

float StreamingService::measuredFps() const {
  return fps.load();
}

CameraConfigurationResult StreamingService::configureCamera(
    framesize_t resolution, uint8_t framebuffers, int jpegQuality) {
  if (!psramFound() && framebuffers > 1) {
    return CameraConfigurationResult::Unsupported;
  }

  bool expected = false;
  if (!cameraReconfiguring.compare_exchange_strong(expected, true)) {
    return CameraConfigurationResult::Busy;
  }

  std::shared_ptr<StreamState> activeStream;
  while (true) {
    if (xSemaphoreTake(cameraMutex, portMAX_DELAY) != pdTRUE) {
      cameraReconfiguring.store(false);
      return CameraConfigurationResult::DeinitFailed;
    }
    activeStream = activeStreamState.lock();
    if (activeStream || !streamActive.load()) {
      break;
    }
    xSemaphoreGive(cameraMutex);
    vTaskDelay(1);
  }

  if (cameraConfig.frame_size == resolution &&
      cameraConfig.fb_count == framebuffers &&
      cameraConfig.jpeg_quality == jpegQuality) {
    xSemaphoreGive(cameraMutex);
    cameraReconfiguring.store(false);
    return CameraConfigurationResult::Unchanged;
  }

  if (activeStream && activeStream->frame != nullptr) {
    esp_camera_fb_return(activeStream->frame);
    activeStream->frame = nullptr;
    activeStream->headerLength = 0;
    activeStream->headerOffset = 0;
    activeStream->frameOffset = 0;
    activeStream->footerOffset = 0;
  }

  const camera_config_t previousConfig = cameraConfig;
  if (esp_camera_deinit() != ESP_OK) {
    xSemaphoreGive(cameraMutex);
    cameraReconfiguring.store(false);
    return CameraConfigurationResult::DeinitFailed;
  }
  cameraReady.store(false);

  cameraConfig.frame_size = resolution;
  cameraConfig.fb_count = framebuffers;
  cameraConfig.jpeg_quality = jpegQuality;
  if (esp_camera_init(&cameraConfig) == ESP_OK) {
    cameraReady.store(true);
    fps.store(0.0f);
    capturedFrame.store(false);
    lastFrameAt.store(0);
    xSemaphoreGive(cameraMutex);
    cameraReconfiguring.store(false);
    return CameraConfigurationResult::Applied;
  }

  Serial.println("Camera reinitialization failed; restoring previous configuration");
  esp_camera_deinit();
  cameraConfig = previousConfig;
  if (esp_camera_init(&cameraConfig) == ESP_OK) {
    cameraReady.store(true);
  }
  xSemaphoreGive(cameraMutex);
  cameraReconfiguring.store(false);
  return CameraConfigurationResult::InitFailed;
}

void StreamingService::writeStatus(JsonObject camera) const {
  framesize_t resolution = CAMERA_FRAME_SIZE;
  int jpegQuality = CAMERA_JPEG_QUALITY;
  int framebuffers = 1;
  bool ready = false;
  bool hasSensorSettings = false;
  int brightness = 0;
  int contrast = 0;
  int saturation = 0;
  int autoExposure = 0;
  int exposureLevel = 0;
  if (xSemaphoreTake(cameraMutex, portMAX_DELAY) == pdTRUE) {
    ready = cameraReady.load();
    resolution = cameraConfig.frame_size;
    jpegQuality = cameraConfig.jpeg_quality;
    framebuffers = cameraConfig.fb_count;
    sensor_t *sensor = ready ? esp_camera_sensor_get() : nullptr;
    if (sensor != nullptr) {
      hasSensorSettings = true;
      brightness = sensor->status.brightness;
      contrast = sensor->status.contrast;
      saturation = sensor->status.saturation;
      autoExposure = sensor->status.aec;
      exposureLevel = sensor->status.ae_level;
    }
    xSemaphoreGive(cameraMutex);
  }

  const char *resolutionName = "unknown";
  switch (resolution) {
    case FRAMESIZE_QQVGA: resolutionName = "QQVGA"; break;
    case FRAMESIZE_QVGA: resolutionName = "QVGA"; break;
    case FRAMESIZE_VGA: resolutionName = "VGA"; break;
    case FRAMESIZE_SVGA: resolutionName = "SVGA"; break;
    case FRAMESIZE_XGA: resolutionName = "XGA"; break;
    case FRAMESIZE_SXGA: resolutionName = "SXGA"; break;
    case FRAMESIZE_UXGA: resolutionName = "UXGA"; break;
    default: break;
  }

  camera["ready"] = ready;
  camera["resolution"] = resolutionName;
  camera["format"] = "JPEG";
  camera["jpegquality"] = jpegQuality;
  camera["framebuffers"] = framebuffers;
  camera["fps"] = measuredFps();
  camera["streamActive"] = isStreamActive();
  camera["frameErrors"] = frameErrorCount();
  camera["hasCapturedFrame"] = hasCapturedFrame();
  if (hasCapturedFrame()) {
    camera["lastFrameAgeMs"] =
        static_cast<uint32_t>(millis() - lastFrameMillis());
  }
  if (hasSensorSettings) {
    JsonObject settings = camera.createNestedObject("settings");
    settings["brightness"] = brightness;
    settings["contrast"] = contrast;
    settings["saturation"] = saturation;
    settings["autoExposure"] = autoExposure;
    settings["exposureLevel"] = exposureLevel;
  }
}

bool StreamingService::isStreamActive() const {
  return streamActive.load();
}

uint32_t StreamingService::frameErrorCount() const {
  return frameErrors.load();
}

bool StreamingService::hasCapturedFrame() const {
  return capturedFrame.load();
}

uint32_t StreamingService::lastFrameMillis() const {
  return lastFrameAt.load();
}

size_t fillStreamChunk(StreamState &state, const std::function<void(float)> &recordFps,
                       const std::function<void()> &recordFrame,
                       const std::function<void()> &recordFrameError,
                       uint8_t *buffer, size_t maxLength) {
  size_t written = 0;
  while (written < maxLength) {
    if (state.frame == nullptr) {
      state.frame = esp_camera_fb_get();
      if (state.frame == nullptr) {
        recordFrameError();
        return written > 0 ? written : RESPONSE_TRY_AGAIN;
      }
      recordFrame();
      state.headerLength = snprintf(
          state.header, sizeof(state.header),
          "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
          static_cast<unsigned>(state.frame->len));
      state.headerOffset = 0;
      state.frameOffset = 0;
      state.footerOffset = 0;
    }

    if (state.headerOffset < state.headerLength) {
      const size_t count = std::min(maxLength - written, state.headerLength - state.headerOffset);
      memcpy(buffer + written, state.header + state.headerOffset, count);
      state.headerOffset += count;
      written += count;
      continue;
    }

    if (state.frameOffset < state.frame->len) {
      const size_t count = std::min(maxLength - written, state.frame->len - state.frameOffset);
      memcpy(buffer + written, state.frame->buf + state.frameOffset, count);
      state.frameOffset += count;
      written += count;
      continue;
    }

    static const uint8_t footer[] = "\r\n";
    if (state.footerOffset < sizeof(footer) - 1) {
      const size_t count = std::min(maxLength - written, sizeof(footer) - 1 - state.footerOffset);
      memcpy(buffer + written, footer + state.footerOffset, count);
      state.footerOffset += count;
      written += count;
      continue;
    }

    esp_camera_fb_return(state.frame);
    state.frame = nullptr;
    ++state.frameCount;
    const uint32_t elapsed = millis() - state.startMillis;
    if (elapsed >= 1000) {
      recordFps(state.frameCount * 1000.0f / elapsed);
    }

    if (written > 0) {
      return written;
    }
    // Il frame precedente era terminato al limite del chunk.
    // Prosegui acquisendo il successivo senza restituire zero.
  }
  return written;

}

void StreamingService::handle(AsyncWebServerRequest *request, ConfigurationService &configuration) {
  if (!configuration.authenticate(request)) {
    return;
  }
  if (cameraReconfiguring.load()) {
    CommunicationUtils::sendError(request, 503, "camera reconfiguration is in progress");
    return;
  }
  bool expected = false;
  if (!streamActive.compare_exchange_strong(expected, true)) {
    CommunicationUtils::sendError(request, 503, "stream is already active");
    return;
  }

  auto state = std::make_shared<StreamState>();
  state->cameraMutex = cameraMutex;
  state->onDestroy = [this]() { streamActive.store(false); };
  if (xSemaphoreTake(cameraMutex, portMAX_DELAY) != pdTRUE) {
    streamActive.store(false);
    CommunicationUtils::sendError(request, 500, "camera lock failed");
    return;
  }
  if (cameraReconfiguring.load() || !cameraReady.load()) {
    xSemaphoreGive(cameraMutex);
    streamActive.store(false);
    CommunicationUtils::sendError(request, 503,
                                   cameraReady.load() ? "camera reconfiguration is in progress"
                                                      : "camera is not ready");
    return;
  }
  activeStreamState = state;
  xSemaphoreGive(cameraMutex);

  std::function<void(float)> recordFps = [this](float value) { fps.store(value); };
  std::function<void()> recordFrame = [this]() {
    lastFrameAt.store(millis());
    capturedFrame.store(true);
  };
  std::function<void()> recordFrameError = [this]() {
    const uint32_t errors = frameErrors.fetch_add(1) + 1;
    if (errors == 1 || errors % 100 == 0) {
      Serial.printf("Camera frame capture failed (%lu total)\n",
                    static_cast<unsigned long>(errors));
    }
  };
  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "multipart/x-mixed-replace; boundary=frame",
      [this, state, recordFps, recordFrame, recordFrameError](uint8_t *buffer, size_t maxLength, size_t) {
        if (cameraReconfiguring.load()) {
          return static_cast<size_t>(RESPONSE_TRY_AGAIN);
        }
        if (xSemaphoreTake(state->cameraMutex, portMAX_DELAY) != pdTRUE) {
          return static_cast<size_t>(0);
        }
        size_t result;
        if (cameraReconfiguring.load()) {
          result = RESPONSE_TRY_AGAIN;
        } else if (!cameraReady.load()) {
          result = 0;
        } else {
          result = fillStreamChunk(*state, recordFps, recordFrame, recordFrameError,
                                   buffer, maxLength);
        }
        xSemaphoreGive(state->cameraMutex);
        return result;
      });
  if (response == nullptr) {
    CommunicationUtils::sendError(request, 500, "could not create camera stream");
    return;
  }
  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}
