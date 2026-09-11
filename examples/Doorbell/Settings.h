#pragma once
#include "BoardProfiles.h"

// LILYGO T-Camera ESP32-WROVER-B / OV2640 on COM14 (camera only).
#ifndef DOORBELL_BOARD
#define DOORBELL_BOARD BOARD_LILYGO_CAMERA
#endif

static const char WIFI_SSID[] = "YOUR_WIFI_SSID";
static const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
// Enter this token in the browser viewer. Change before using on your LAN.
static const char VIEWER_TOKEN[] = "1234";
static constexpr bool WEBRTC_DEBUG = false; // Includes ephemeral SDP/ICE details in Serial output.
// Optional external ring button to GND. -1 uses Serial Monitor 'r' instead.
// Select an unused GPIO; do not reuse a camera, microphone or flash/PSRAM pin.
static constexpr int RING_BUTTON_PIN = -1;
// Enable onboard microphone on XIAO Sense; other profiles default to camera only.
#ifndef DOORBELL_MIC
#define DOORBELL_MIC (DOORBELL_BOARD == BOARD_XIAO_S3_SENSE)
#endif
