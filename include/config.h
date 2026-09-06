#pragma once

// Registration PIN expected by the mobile client.
#define DEVICE_PIN "482917"

// Device identity returned by GET /status.
#define DEVICE_TYPE "ESP32-CAM"
#define DEVICE_VERSION "1.0"
#define DEVICE_NAME "ESP32-CAM-AI"
#define DEVICE_SERIAL "1234567890"

// Credentials and channel used by the Wi-Fi access point.
#define AP_SSID DEVICE_NAME
#define AP_PASSWORD "esp32cam-local"
#define AP_CHANNEL 6

// Camera capture settings. JPEG quality uses the camera scale: lower is better.
#define CAMERA_FRAME_SIZE FRAMESIZE_VGA
#define CAMERA_JPEG_QUALITY 12
#define CAMERA_FB_COUNT 2

// Maximum allowed timestamp difference for authenticated requests, in seconds.
#define AUTH_WINDOW_SECONDS 5

// Battery measurement. Set the ADC GPIO to -1 when no battery circuit is fitted.
#define BATTERY_ADC_PIN -1
// Voltage range used to convert the measured battery voltage into a percentage.
#define BATTERY_MIN_MV 3300
#define BATTERY_MAX_MV 4200
// Multiplier for the resistor-divider ratio: battery voltage / ADC voltage.
#define BATTERY_DIVIDER_RATIO 2.0f

// Addressable LED strip hardware. Set enabled to 1 and count to the real LED count.
#define LED_STRIP_ENABLED 0
#define LED_STRIP_GPIO 13
#define LED_STRIP_COUNT 0

// Startup defaults; values received by POST /led are saved in NVS.
#define LED_STRIP_BRIGHTNESS 0
#define LED_STRIP_MODE "off"
#define LED_STRIP_INTERVAL_MILLIS 1000
#define LED_STRIP_RED 255
#define LED_STRIP_GREEN 255
#define LED_STRIP_BLUE 255

// WS2812B / SP620 protocol: one-wire data, 800 kHz, GRB byte order.
#define LED_STRIP_COLOR_ORDER NEO_GRB
#define LED_STRIP_SIGNAL_SPEED NEO_KHZ800
