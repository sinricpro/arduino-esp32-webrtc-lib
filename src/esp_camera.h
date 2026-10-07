// The esp32-camera API on every target. The Arduino core ships esp32-camera for ESP32 and ESP32-S3;
// for the ESP32-P4, whose camera is MIPI-CSI, this library provides it (src/p4_camera).
#pragma once
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "p4_camera/esp_camera_p4.h"
#else
#include_next <esp_camera.h>
#endif
