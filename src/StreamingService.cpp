#include "StreamingService.h"

#include <Arduino.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <esp_heap_caps.h>
#include <esp_timer.h>

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

struct CapturedFrame
{
  uint8_t *buf = nullptr;
  size_t len = 0;
  size_t capacity = 0;
  uint32_t capturedAt = 0;
  uint32_t sequence = 0;

  ~CapturedFrame() { free(buf); }
};

struct StreamState
{
  std::shared_ptr<CapturedFrame> frame;
  AsyncClient *client = nullptr;
  bool stopped = false;
  uint32_t lastSequence = 0;
  uint32_t sendStartedAt = 0;
  char header[96] = {};
  size_t headerLength = 0;
  size_t headerOffset = 0;
  size_t frameOffset = 0;
  size_t footerOffset = 0;
  uint32_t frameCount = 0;
  uint32_t startMillis = millis();
  std::function<void()> onDestroy;

  ~StreamState()
  {
    if (onDestroy)
    {
      onDestroy();
    }
  }
};

bool StreamingService::beginCamera()
{
  cameraMutex = xSemaphoreCreateMutex();
  frameMutex = xSemaphoreCreateMutex();
  if (cameraMutex == nullptr || frameMutex == nullptr)
  {
    Serial.println("Camera mutex initialization failed");
    return false;
  }
  initializeCameraConfig();
  if (xSemaphoreTake(cameraMutex, portMAX_DELAY) != pdTRUE)
  {
    return false;
  }
  const bool initialized = esp_camera_init(&cameraConfig) == ESP_OK;
  cameraReady.store(initialized);
  if (initialized)
  {
    cacheSensorSettings();
  }
  xSemaphoreGive(cameraMutex);
  if (!initialized)
  {
    return false;
  }
  if (xTaskCreate([](void *context)
                  { static_cast<StreamingService *>(context)->captureLoop(); }, "cameraCapture", 4096, this, 1, nullptr) != pdPASS)
  {
    cameraReady.store(false);
    esp_camera_deinit();
    return false;
  }
  return true;
}

void StreamingService::cacheSensorSettings()
{
  sensor_t *sensor = esp_camera_sensor_get();
  hasSensorSettings = sensor != nullptr;
  if (sensor != nullptr)
  {
    brightness = sensor->status.brightness;
    contrast = sensor->status.contrast;
    saturation = sensor->status.saturation;
    autoExposure = sensor->status.aec;
    exposureLevel = sensor->status.ae_level;
  }
}

void StreamingService::captureLoop()
{
  std::shared_ptr<CapturedFrame> slots[3];
  for (auto &slot : slots)
  {
    slot = std::make_shared<CapturedFrame>();
  }
  uint32_t sequence = 0;
  uint32_t count = 0;
  uint32_t startedAt = millis();
  for (;;)
  {
    if (cameraReconfiguring.load() || !cameraReady.load() ||
        xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(20)) != pdTRUE)
    {
      vTaskDelay(1);
      continue;
    }
    if (cameraReconfiguring.load() || !cameraReady.load())
    {
      xSemaphoreGive(cameraMutex);
      vTaskDelay(1);
      continue;
    }
    const int64_t acquisitionStarted = esp_timer_get_time();
    camera_fb_t *frame = esp_camera_fb_get();
    captureDurationUs.store(static_cast<uint32_t>(esp_timer_get_time() - acquisitionStarted));
    if (frame == nullptr)
    {
      frameErrors.fetch_add(1);
      xSemaphoreGive(cameraMutex);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    ++count;
    const uint32_t elapsed = millis() - startedAt;
    if (elapsed >= 1000)
    {
      captureFps.store(count * 1000.0f / elapsed);
      count = 0;
      startedAt = millis();
    }
    std::shared_ptr<CapturedFrame> next;
    for (auto &slot : slots)
    {
      if (slot.use_count() == 1)
      {
        next = slot;
        break;
      }
    }
    if (next && next->capacity < frame->len)
    {
      auto *storage = static_cast<uint8_t *>(heap_caps_realloc(
          next->buf, frame->len,
          MALLOC_CAP_8BIT | (psramFound() ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL)));
      if (storage != nullptr)
      {
        next->buf = storage;
        next->capacity = frame->len;
      }
    }
    if (next && next->capacity >= frame->len)
    {
      memcpy(next->buf, frame->buf, frame->len);
      next->len = frame->len;
      const int64_t timestampUs = static_cast<int64_t>(frame->timestamp.tv_sec) * 1000000 +
                                  frame->timestamp.tv_usec;
      next->capturedAt = static_cast<uint32_t>(timestampUs / 1000);
      next->sequence = ++sequence;
    }
    else
    {
      next.reset();
      frameErrors.fetch_add(1);
    }
    esp_camera_fb_return(frame);
    if (next)
    {
      xSemaphoreTake(frameMutex, portMAX_DELAY);
      latestFrame = next;
      lastFrameAt.store(next->capturedAt);
      capturedFrame.store(true);
      xSemaphoreGive(frameMutex);
    }
    xSemaphoreGive(cameraMutex);
    vTaskDelay(1);
  }
}

void StreamingService::initializeCameraConfig()
{
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

  if (psramFound())
  {
    cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
  }
  else
  {
    cameraConfig.fb_count = 1;
    cameraConfig.fb_location = CAMERA_FB_IN_DRAM;
  }
}

float StreamingService::measuredFps() const
{
  return fps.load();
}

CameraConfigurationResult StreamingService::configureCamera(
    framesize_t resolution, uint8_t framebuffers, int jpegQuality)
{
  if (!psramFound() && framebuffers > 1)
  {
    return CameraConfigurationResult::Unsupported;
  }

  bool expected = false;
  if (!cameraReconfiguring.compare_exchange_strong(expected, true))
  {
    return CameraConfigurationResult::Busy;
  }

  if (xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(50)) != pdTRUE)
  {
    cameraReconfiguring.store(false);
    return CameraConfigurationResult::Busy;
  }

  if (cameraConfig.frame_size == resolution &&
      cameraConfig.fb_count == framebuffers &&
      cameraConfig.jpeg_quality == jpegQuality)
  {
    xSemaphoreGive(cameraMutex);
    cameraReconfiguring.store(false);
    return CameraConfigurationResult::Unchanged;
  }

  const camera_config_t previousConfig = cameraConfig;
  if (esp_camera_deinit() != ESP_OK)
  {
    xSemaphoreGive(cameraMutex);
    cameraReconfiguring.store(false);
    return CameraConfigurationResult::DeinitFailed;
  }
  cameraReady.store(false);
  xSemaphoreTake(frameMutex, portMAX_DELAY);
  latestFrame.reset();
  xSemaphoreGive(frameMutex);
  capturedFrame.store(false);
  captureFps.store(0.0f);

  cameraConfig.frame_size = resolution;
  cameraConfig.fb_count = framebuffers;
  cameraConfig.jpeg_quality = jpegQuality;
  if (esp_camera_init(&cameraConfig) == ESP_OK)
  {
    cameraReady.store(true);
    cacheSensorSettings();
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
  if (esp_camera_init(&cameraConfig) == ESP_OK)
  {
    cameraReady.store(true);
    cacheSensorSettings();
  }
  xSemaphoreGive(cameraMutex);
  cameraReconfiguring.store(false);
  return CameraConfigurationResult::InitFailed;
}

void StreamingService::writeStatus(JsonObject camera) const
{
  const framesize_t resolution = cameraConfig.frame_size;

  const char *resolutionName = "unknown";
  switch (resolution)
  {
  case FRAMESIZE_QQVGA:
    resolutionName = "QQVGA";
    break;
  case FRAMESIZE_QVGA:
    resolutionName = "QVGA";
    break;
  case FRAMESIZE_VGA:
    resolutionName = "VGA";
    break;
  case FRAMESIZE_SVGA:
    resolutionName = "SVGA";
    break;
  case FRAMESIZE_XGA:
    resolutionName = "XGA";
    break;
  case FRAMESIZE_SXGA:
    resolutionName = "SXGA";
    break;
  case FRAMESIZE_UXGA:
    resolutionName = "UXGA";
    break;
  default:
    break;
  }

  camera["ready"] = cameraReady.load();
  camera["reconfiguring"] = cameraReconfiguring.load();
  camera["resolution"] = resolutionName;
  camera["format"] = "JPEG";
  camera["jpegquality"] = cameraConfig.jpeg_quality;
  camera["framebuffers"] = cameraConfig.fb_count;
  camera["fps"] = measuredFps();
  camera["captureFps"] = captureFps.load();
  camera["captureMs"] = captureDurationUs.load() / 1000.0f;
  camera["frameChunkMs"] = frameChunkMs.load();
  camera["frameChunkAgeMs"] = frameChunkAgeMs.load();
  camera["streamActive"] = isStreamActive();
  camera["frameErrors"] = frameErrorCount();
  camera["hasCapturedFrame"] = hasCapturedFrame();
  if (hasCapturedFrame())
  {
    camera["lastFrameAgeMs"] =
        static_cast<uint32_t>(millis() - lastFrameMillis());
  }
  if (hasSensorSettings)
  {
    JsonObject settings = camera.createNestedObject("settings");
    settings["brightness"] = brightness;
    settings["contrast"] = contrast;
    settings["saturation"] = saturation;
    settings["autoExposure"] = autoExposure;
    settings["exposureLevel"] = exposureLevel;
  }
}

bool StreamingService::isStreamActive() const
{
  return streamActive.load();
}

uint32_t StreamingService::frameErrorCount() const
{
  return frameErrors.load();
}

bool StreamingService::hasCapturedFrame() const
{
  return capturedFrame.load();
}

uint32_t StreamingService::lastFrameMillis() const
{
  return lastFrameAt.load();
}

size_t fillStreamChunk(StreamState &state,
                       const std::function<std::shared_ptr<CapturedFrame>()> &getLatest,
                       const std::function<void(uint32_t, uint32_t)> &recordSent,
                       uint8_t *buffer, size_t maxLength)
{
  size_t written = 0;
  while (written < maxLength)
  {
    if (state.frame == nullptr)
    {
      state.frame = getLatest();
      if (!state.frame || state.frame->sequence == state.lastSequence)
      {
        state.frame.reset();
        return written > 0 ? written : RESPONSE_TRY_AGAIN;
      }
      state.sendStartedAt = millis();
      state.headerLength = snprintf(
          state.header, sizeof(state.header),
          "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
          static_cast<unsigned>(state.frame->len));
      state.headerOffset = 0;
      state.frameOffset = 0;
      state.footerOffset = 0;
    }

    if (state.headerOffset < state.headerLength)
    {
      const size_t count = std::min(maxLength - written, state.headerLength - state.headerOffset);
      memcpy(buffer + written, state.header + state.headerOffset, count);
      state.headerOffset += count;
      written += count;
      continue;
    }

    if (state.frameOffset < state.frame->len)
    {
      const size_t count = std::min(maxLength - written, state.frame->len - state.frameOffset);
      memcpy(buffer + written, state.frame->buf + state.frameOffset, count);
      state.frameOffset += count;
      written += count;
      continue;
    }

    static const uint8_t footer[] = "\r\n";
    if (state.footerOffset < sizeof(footer) - 1)
    {
      const size_t count = std::min(maxLength - written, sizeof(footer) - 1 - state.footerOffset);
      memcpy(buffer + written, footer + state.footerOffset, count);
      state.footerOffset += count;
      written += count;
      continue;
    }

    recordSent(millis() - state.sendStartedAt, millis() - state.frame->capturedAt);
    state.lastSequence = state.frame->sequence;
    state.frame.reset();

    if (written > 0)
    {
      return written;
    }
  }
  return written;
}

void StreamingService::handle(AsyncWebServerRequest *request, ConfigurationService &configuration)
{
  if (!configuration.authenticate(request))
  {
    return;
  }
  if (cameraReconfiguring.load())
  {
    CommunicationUtils::sendError(request, 503, "camera reconfiguration is in progress");
    return;
  }
  if (!cameraReady.load())
  {
    CommunicationUtils::sendError(request, 503, "camera is not ready");
    return;
  }

  auto previous = activeStreamState.lock();
  if (previous)
  {
    previous->stopped = true;
    previous->frame.reset();
    if (previous->client != nullptr)
    {
      previous->client->close();
    }
  }
  auto state = std::make_shared<StreamState>();
  state->client = request->client();
  activeStreamState = state;
  streamActive.store(true);
  fps.store(0.0f);
  std::weak_ptr<StreamState> weakState = state;
  state->onDestroy = [this, weakState]()
  {
    if (!activeStreamState.owner_before(weakState) &&
        !weakState.owner_before(activeStreamState))
    {
      streamActive.store(false);
      fps.store(0.0f);
    }
  };
  request->onDisconnect([this, weakState]()
                        {
    if (auto disconnected = weakState.lock()) {
      disconnected->stopped = true;
      disconnected->client = nullptr;
      disconnected->frame.reset();
      if (activeStreamState.lock() == disconnected) {
        activeStreamState.reset();
        streamActive.store(false);
        fps.store(0.0f);
      }
    } });
  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "multipart/x-mixed-replace; boundary=frame",
      [this, state](uint8_t *buffer, size_t maxLength, size_t)
      {
        if (state->stopped)
        {
          return static_cast<size_t>(0);
        }
        if (!cameraReady.load() && !cameraReconfiguring.load() && !state->frame)
        {
          return static_cast<size_t>(0);
        }
        return fillStreamChunk(*state, [this]()
                               {
          std::shared_ptr<CapturedFrame> frame;
          if (xSemaphoreTake(frameMutex, 0) == pdTRUE) {
            frame = latestFrame;
            xSemaphoreGive(frameMutex);
          }
          return frame; }, [this, state](uint32_t duration, uint32_t age)
                               {
          frameChunkMs.store(duration);
          frameChunkAgeMs.store(age);
          ++state->frameCount;
          const uint32_t elapsed = millis() - state->startMillis;
          if (elapsed >= 1000) {
            fps.store(state->frameCount * 1000.0f / elapsed);
            state->frameCount = 0;
            state->startMillis = millis();
          } }, buffer, maxLength);
      });
  if (response == nullptr)
  {
    CommunicationUtils::sendError(request, 500, "could not create camera stream");
    return;
  }
  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}
