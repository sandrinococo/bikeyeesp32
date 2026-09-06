#include "StreamingService.h"

#include <Arduino.h>

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
  return fps;
}

void StreamingService::handle(WebServer &server, ConfigurationService &configuration) {
  if (!configuration.authenticate(server)) {
    return;
  }

  WiFiClient client = server.client();
  uint32_t frameCount = 0;
  const uint32_t streamStart = millis();
  server.sendContent("HTTP/1.1 200 OK\r\n"
                     "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                     "Cache-Control: no-cache\r\n\r\n");

  while (client.connected()) {
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr) {
      break;
    }
    client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", frame->len);
    client.write(frame->buf, frame->len);
    client.print("\r\n");
    esp_camera_fb_return(frame);
    ++frameCount;
    const uint32_t elapsed = millis() - streamStart;
    if (elapsed >= 1000) {
      fps = frameCount * 1000.0f / elapsed;
    }
    delay(30);
  }
  client.stop();
}