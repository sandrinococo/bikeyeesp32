#pragma once

// PIN required to register a mobile client.
#define DEVICE_PIN "482917"

// Device type reported by GET /status.
#define DEVICE_TYPE "ESP32-CAM"
// Firmware version reported by GET /status.
#define DEVICE_VERSION "1.0"
// Human-readable device name and access-point SSID.
#define DEVICE_NAME "ESP32-CAM-AI"
// Device serial number reported by GET /status.
#define DEVICE_SERIAL "1234567890"

// Wi-Fi access-point network name.
#define AP_SSID DEVICE_NAME
// Wi-Fi access-point password.
#define AP_PASSWORD "esp32cam-local"
// Wi-Fi channel used by the access point.
#define AP_CHANNEL 6
// Maximum number of stations allowed to connect to the access point.
#define AP_MAX_CONNECTIONS 1
// Maximum seconds a station may remain inactive before the AP disconnects it.
#define AP_INACTIVE_TIMEOUT_SECONDS 65535

// Camera frame size used by the MJPEG stream.
#define CAMERA_FRAME_SIZE FRAMESIZE_VGA
// JPEG quality value; lower values produce better image quality.
#define CAMERA_JPEG_QUALITY 12
// Number of frame buffers allocated by the camera driver.
#define CAMERA_FB_COUNT 2

// Maximum timestamp difference accepted for authenticated requests, in seconds.
#define AUTH_WINDOW_SECONDS 5

// Disable Wi-Fi power saving to improve AP and stream stability.
#define WIFI_DISABLE_POWER_SAVE 1

// ADC GPIO used to measure battery voltage; -1 disables battery measurement.
#define BATTERY_ADC_PIN -1
// Battery voltage corresponding to 0% charge, in millivolts.
#define BATTERY_MIN_MV 3300
// Battery voltage corresponding to 100% charge, in millivolts.
#define BATTERY_MAX_MV 4200
// Voltage-divider multiplier: battery voltage divided by ADC voltage.
#define BATTERY_DIVIDER_RATIO 2.0f
// Interval between battery ADC samples, in milliseconds.
#define BATTERY_SAMPLE_INTERVAL_MILLIS 1000
// Number of samples used to calculate the moving average.
#define BATTERY_AVERAGE_SAMPLES 10
// Charger status GPIO; -1 disables charger status detection.
#define BATTERY_CHARGE_STATUS_PIN -1
// Digital level that indicates that the charger is active.
#define BATTERY_CHARGE_STATUS_ACTIVE_LEVEL LOW
// Enable the internal pull-up on the charger status GPIO.
#define BATTERY_CHARGE_STATUS_PULLUP 1
// Battery percentage below which the internal LED indicates low battery.
#define BATTERY_LOW_PERCENT 20
// Battery percentage at or above which the internal LED indicates full charge.
#define BATTERY_FULL_PERCENT 100
// GPIO connected to the internal status LED.
#define INTERNAL_LED_GPIO 4
// Digital level that turns the internal LED on.
#define INTERNAL_LED_ACTIVE_LEVEL 1
// Low-battery LED toggle interval, in milliseconds.
#define INTERNAL_LED_LOW_BATTERY_INTERVAL_MILLIS 250
// Charging LED toggle interval, in milliseconds.
#define INTERNAL_LED_CHARGING_INTERVAL_MILLIS 1000

// Enable the addressable LED strip when set to 1.
#define LED_STRIP_ENABLED 0
// GPIO connected to the addressable LED strip.
#define LED_STRIP_GPIO 13
// Number of LEDs in the addressable strip.
#define LED_STRIP_COUNT 0

// Default LED brightness; values received by POST /led are saved in NVS.
#define LED_STRIP_BRIGHTNESS 0
// Default LED mode.
#define LED_STRIP_MODE "off"
// Default blink interval, in milliseconds.
#define LED_STRIP_INTERVAL_MILLIS 1000
// Default red color component.
#define LED_STRIP_RED 255
// Default green color component.
#define LED_STRIP_GREEN 255
// Default blue color component.
#define LED_STRIP_BLUE 255

// LED strip color order used by the WS2812B/SP620 protocol.
#define LED_STRIP_COLOR_ORDER NEO_GRB
// LED strip signal frequency used by the WS2812B/SP620 protocol.
#define LED_STRIP_SIGNAL_SPEED NEO_KHZ800
