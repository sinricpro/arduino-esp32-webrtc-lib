#pragma once

#include <Arduino.h>
#include <esp_camera.h>

// These presets belong to the sketch. Edit pins and capture settings here.
// Sources: Espressif camera_pinout.h and LilyGO/esp32-camera-bme280.
// ESP32-S3 WROOM boards do not all share one wiring layout: check PWDN too.
namespace CameraSetup {

enum class Board {
    EspEye,
    XiaoS3Sense,
    FreenoveS3,
    M5Camera,
    M5CameraB,
    AiThinker,
    WroverKit,
    Esp32S3Wroom,
    Esp32S3Goouuu,
    LilygoCamera,
};

inline const char *name(Board board) {
    switch (board) {
    case Board::EspEye:
        return "ESP-EYE";
    case Board::XiaoS3Sense:
        return "XIAO ESP32S3 Sense";
    case Board::FreenoveS3:
        return "Freenove ESP32-S3";
    case Board::M5Camera:
        return "M5Camera A";
    case Board::M5CameraB:
        return "M5Camera B";
    case Board::AiThinker:
        return "AI-Thinker ESP32-CAM";
    case Board::WroverKit:
        return "ESP-WROVER-KIT";
    case Board::Esp32S3Wroom:
        return "ESP32-S3 WROOM (PWDN 38)";
    case Board::Esp32S3Goouuu:
        return "GOOUUU ESP32-S3";
    case Board::LilygoCamera:
        return "LILYGO camera-bme280 (camera only)";
    }
    return "Unknown camera profile";
}

inline camera_config_t config(Board board) {
    camera_config_t camera = {};
    camera.pin_pwdn = -1;
    camera.pin_reset = -1;
    camera.ledc_channel = LEDC_CHANNEL_0;
    camera.ledc_timer = LEDC_TIMER_0;
    camera.xclk_freq_hz = 20000000;
    camera.pixel_format = PIXFORMAT_JPEG;
    // QVGA = 320x240, VGA = 640x480, SVGA = 800x600.
    // Higher resolutions must still fit the viewer's 128 KiB JPEG limit.
    camera.frame_size = FRAMESIZE_VGA;
    camera.jpeg_quality = 16;
    camera.fb_count = 2;
    camera.fb_location = CAMERA_FB_IN_PSRAM;
    camera.grab_mode = CAMERA_GRAB_LATEST;

    switch (board) {
    case Board::EspEye: // ESP-EYE
        camera.pin_d0 = 34;
        camera.pin_d1 = 13;
        camera.pin_d2 = 14;
        camera.pin_d3 = 35;
        camera.pin_d4 = 39;
        camera.pin_d5 = 38;
        camera.pin_d6 = 37;
        camera.pin_d7 = 36;
        camera.pin_xclk = 4;
        camera.pin_sccb_sda = 18;
        camera.pin_sccb_scl = 23;
        camera.pin_vsync = 5;
        camera.pin_href = 27;
        camera.pin_pclk = 25;
        camera.pin_pwdn = -1;
        camera.pin_reset = -1;
        break;

    case Board::XiaoS3Sense: // XIAO ESP32S3 Sense
        camera.pin_d0 = 15;
        camera.pin_d1 = 17;
        camera.pin_d2 = 18;
        camera.pin_d3 = 16;
        camera.pin_d4 = 14;
        camera.pin_d5 = 12;
        camera.pin_d6 = 11;
        camera.pin_d7 = 48;
        camera.pin_xclk = 10;
        camera.pin_sccb_sda = 40;
        camera.pin_sccb_scl = 39;
        camera.pin_vsync = 38;
        camera.pin_href = 47;
        camera.pin_pclk = 13;
        camera.pin_pwdn = -1;
        camera.pin_reset = -1;
        break;

    case Board::FreenoveS3: // Freenove ESP32-S3
        camera.pin_d0 = 11;
        camera.pin_d1 = 9;
        camera.pin_d2 = 8;
        camera.pin_d3 = 10;
        camera.pin_d4 = 12;
        camera.pin_d5 = 18;
        camera.pin_d6 = 17;
        camera.pin_d7 = 16;
        camera.pin_xclk = 15;
        camera.pin_sccb_sda = 4;
        camera.pin_sccb_scl = 5;
        camera.pin_vsync = 6;
        camera.pin_href = 7;
        camera.pin_pclk = 13;
        camera.pin_pwdn = -1;
        camera.pin_reset = -1;
        break;

    case Board::M5Camera: // M5Camera A
        camera.pin_d0 = 32;
        camera.pin_d1 = 35;
        camera.pin_d2 = 34;
        camera.pin_d3 = 5;
        camera.pin_d4 = 39;
        camera.pin_d5 = 18;
        camera.pin_d6 = 36;
        camera.pin_d7 = 19;
        camera.pin_xclk = 27;
        camera.pin_sccb_sda = 25;
        camera.pin_sccb_scl = 23;
        camera.pin_vsync = 22;
        camera.pin_href = 26;
        camera.pin_pclk = 21;
        camera.pin_pwdn = -1;
        camera.pin_reset = 15;
        break;

    case Board::M5CameraB: // M5Camera B
        camera.pin_d0 = 32;
        camera.pin_d1 = 35;
        camera.pin_d2 = 34;
        camera.pin_d3 = 5;
        camera.pin_d4 = 39;
        camera.pin_d5 = 18;
        camera.pin_d6 = 36;
        camera.pin_d7 = 19;
        camera.pin_xclk = 27;
        camera.pin_sccb_sda = 22;
        camera.pin_sccb_scl = 23;
        camera.pin_vsync = 25;
        camera.pin_href = 26;
        camera.pin_pclk = 21;
        camera.pin_pwdn = -1;
        camera.pin_reset = 15;
        break;

    case Board::AiThinker: // AI-Thinker ESP32-CAM
        camera.pin_d0 = 5;
        camera.pin_d1 = 18;
        camera.pin_d2 = 19;
        camera.pin_d3 = 21;
        camera.pin_d4 = 36;
        camera.pin_d5 = 39;
        camera.pin_d6 = 34;
        camera.pin_d7 = 35;
        camera.pin_xclk = 0;
        camera.pin_sccb_sda = 26;
        camera.pin_sccb_scl = 27;
        camera.pin_vsync = 25;
        camera.pin_href = 23;
        camera.pin_pclk = 22;
        camera.pin_pwdn = 32;
        camera.pin_reset = -1;
        break;

    case Board::WroverKit: // ESP-WROVER-KIT
        camera.pin_d0 = 4;
        camera.pin_d1 = 5;
        camera.pin_d2 = 18;
        camera.pin_d3 = 19;
        camera.pin_d4 = 36;
        camera.pin_d5 = 39;
        camera.pin_d6 = 34;
        camera.pin_d7 = 35;
        camera.pin_xclk = 21;
        camera.pin_sccb_sda = 26;
        camera.pin_sccb_scl = 27;
        camera.pin_vsync = 25;
        camera.pin_href = 23;
        camera.pin_pclk = 22;
        camera.pin_pwdn = -1;
        camera.pin_reset = -1;
        break;

    case Board::Esp32S3Wroom: // ESP32-S3 WROOM (PWDN 38)
        camera.pin_d0 = 11;
        camera.pin_d1 = 9;
        camera.pin_d2 = 8;
        camera.pin_d3 = 10;
        camera.pin_d4 = 12;
        camera.pin_d5 = 18;
        camera.pin_d6 = 17;
        camera.pin_d7 = 16;
        camera.pin_xclk = 15;
        camera.pin_sccb_sda = 4;
        camera.pin_sccb_scl = 5;
        camera.pin_vsync = 6;
        camera.pin_href = 7;
        camera.pin_pclk = 13;
        camera.pin_pwdn = 38;
        camera.pin_reset = -1;
        break;

    case Board::Esp32S3Goouuu: // GOOUUU ESP32-S3
        camera.pin_d0 = 11;
        camera.pin_d1 = 9;
        camera.pin_d2 = 8;
        camera.pin_d3 = 10;
        camera.pin_d4 = 12;
        camera.pin_d5 = 18;
        camera.pin_d6 = 17;
        camera.pin_d7 = 16;
        camera.pin_xclk = 15;
        camera.pin_sccb_sda = 4;
        camera.pin_sccb_scl = 5;
        camera.pin_vsync = 6;
        camera.pin_href = 7;
        camera.pin_pclk = 13;
        camera.pin_pwdn = -1;
        camera.pin_reset = -1;
        break;

    case Board::LilygoCamera: // LILYGO camera-bme280 (camera only)
        camera.pin_d0 = 5;
        camera.pin_d1 = 14;
        camera.pin_d2 = 4;
        camera.pin_d3 = 15;
        camera.pin_d4 = 18;
        camera.pin_d5 = 23;
        camera.pin_d6 = 36;
        camera.pin_d7 = 39;
        camera.pin_xclk = 32;
        camera.pin_sccb_sda = 13;
        camera.pin_sccb_scl = 12;
        camera.pin_vsync = 27;
        camera.pin_href = 25;
        camera.pin_pclk = 19;
        camera.pin_pwdn = 26;
        camera.pin_reset = -1;
        // At 20 MHz the camera interferes with this board's Wi-Fi: TX buffers stop draining and
        // sendto() fails with ENOMEM once a stream reaches VGA.
        camera.xclk_freq_hz = 10000000;
        break;
    }
    return camera;
}

// ESP-EYE needs pull-ups before probing its camera. Other profiles do not.
inline void prepare(Board board) {
    if (board == Board::EspEye) {
        pinMode(13, INPUT_PULLUP);
        pinMode(14, INPUT_PULLUP);
    }
}

} // namespace CameraSetup
