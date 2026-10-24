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

bool StreamingService::beginCamera() {
  camera_config_t cameraConfig = {};
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
  cameraConfig.fb_count = CAMERA_FB_COUNT;
  cameraConfig.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    cameraConfig.fb_count = 1;
    cameraConfig.fb_location = CAMERA_FB_IN_DRAM;
  }
  return esp_camera_init(&cameraConfig) == ESP_OK;
}

float StreamingService::measuredFps() const {
  return fps.load();
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

namespace {
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

  ~StreamState() {
    if (frame != nullptr) {
      esp_camera_fb_return(frame);
    }
    if (onDestroy) {
      onDestroy();
    }
  }
};

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
}  // namespace

void StreamingService::handle(AsyncWebServerRequest *request, ConfigurationService &configuration) {
  if (!configuration.authenticate(request)) {
    return;
  }
  bool expected = false;
  if (!streamActive.compare_exchange_strong(expected, true)) {
    CommunicationUtils::sendError(request, 503, "stream is already active");
    return;
  }

  auto state = std::make_shared<StreamState>();
  state->onDestroy = [this]() { streamActive.store(false); };
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
      [state, recordFps, recordFrame, recordFrameError](uint8_t *buffer, size_t maxLength, size_t) {
        return fillStreamChunk(*state, recordFps, recordFrame, recordFrameError,
                               buffer, maxLength);
      });
  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}
