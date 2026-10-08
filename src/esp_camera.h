// The esp32-camera API on every target. The Arduino core ships esp32-camera for ESP32 and ESP32-S3;
// for the ESP32-P4, whose camera is MIPI-CSI, this library provides it (src/p4_camera).
#pragma once
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4
// Core 3.3.10 and earlier ship neither esp_cam_sensor nor the hardware H.264 encoder for the P4
#if !__has_include("esp_cam_sensor.h") || !__has_include("esp_h264_enc_single_hw.h")
#error "ESP32-P4 camera support needs Arduino ESP32 core 3.3.11 or later."
#endif
#include "p4_camera/esp_camera_p4.h"
#else
#include_next <esp_camera.h>
#endif
