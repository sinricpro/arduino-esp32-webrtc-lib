#pragma once
#include <Arduino.h>
#include <esp_camera.h>

namespace WebRTCCamera {
// The sketch owns pin mappings, sensor settings, and board-specific GPIO setup.
// Pass a complete configuration; the camera driver copies it during initialization.
inline esp_err_t begin(const camera_config_t &config) {
    if (config.fb_location == CAMERA_FB_IN_PSRAM && !psramFound())
        return ESP_ERR_NO_MEM;
    return esp_camera_init(&config);
}
}
