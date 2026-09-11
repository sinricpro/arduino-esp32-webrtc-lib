#pragma once
// Select one profile; these numbers are also used by the compile-matrix script.
#define BOARD_ESP_EYE 1
#define BOARD_XIAO_S3_SENSE 2
#define BOARD_FREENOVE_S3 3
#define BOARD_M5CAMERA 4
#define BOARD_M5CAMERA_B 5
#define BOARD_AI_THINKER 6
#ifndef DOORBELL_BOARD
#define DOORBELL_BOARD BOARD_XIAO_S3_SENSE
#endif

static const char WIFI_SSID[] = "YOUR_WIFI_SSID";
static const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
// Enter this token in the browser viewer. Change before using on your LAN.
static const char VIEWER_TOKEN[] = "change-this-token";
static constexpr bool WEBRTC_DEBUG = false; // Includes ephemeral SDP/ICE details in Serial output.
// Optional external ring button to GND. -1 uses Serial Monitor 'r' instead.
// Select an unused GPIO; do not reuse a camera, microphone or flash/PSRAM pin.
static constexpr int RING_BUTTON_PIN = -1;
// Enable onboard microphone on XIAO Sense; other profiles default to camera only.
#ifndef DOORBELL_MIC
#define DOORBELL_MIC (DOORBELL_BOARD == BOARD_XIAO_S3_SENSE)
#endif
