#pragma once
#include <Arduino.h>
#include <esp_camera.h>

namespace CameraSetup {
enum class Board { EspEye, XiaoS3Sense, FreenoveS3, M5Camera, M5CameraB, AiThinker };
// M5Camera = original model A pinout. Model B uses different SCCB/VSYNC pins.
inline camera_config_t config(Board board) {
    camera_config_t c = {};
    c.pin_pwdn = -1; c.pin_reset = -1;
    c.ledc_channel = LEDC_CHANNEL_0; c.ledc_timer = LEDC_TIMER_0;
    c.xclk_freq_hz = 20000000;
    c.pixel_format = PIXFORMAT_JPEG; c.frame_size = FRAMESIZE_QVGA;
    c.jpeg_quality = 16; c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM; c.grab_mode = CAMERA_GRAB_LATEST;
    // D0..D7, XCLK, SDA, SCL, VSYNC, HREF, PCLK.
    const int eye[] = {34,13,14,35,39,38,37,36,4,18,23,5,27,25};
    const int xiao[] = {15,17,18,16,14,12,11,48,10,40,39,38,47,13};
    const int freenove[] = {11,9,8,10,12,18,17,16,15,4,5,6,7,13};
    const int m5a[] = {32,35,34,5,39,18,36,19,27,25,23,22,26,21};
    const int m5b[] = {32,35,34,5,39,18,36,19,27,22,23,25,26,21};
    const int ai[] = {5,18,19,21,36,39,34,35,0,26,27,25,23,22};
    const int *p = eye;
    switch (board) {
        case Board::XiaoS3Sense: p = xiao; break;
        case Board::FreenoveS3: p = freenove; break;
        case Board::M5Camera: p = m5a; c.pin_reset = 15; break;
        case Board::M5CameraB: p = m5b; c.pin_reset = 15; break;
        case Board::AiThinker: p = ai; c.pin_pwdn = 32; break;
        default: break;
    }
    c.pin_d0=p[0]; c.pin_d1=p[1]; c.pin_d2=p[2]; c.pin_d3=p[3];
    c.pin_d4=p[4]; c.pin_d5=p[5]; c.pin_d6=p[6]; c.pin_d7=p[7];
    c.pin_xclk=p[8]; c.pin_sccb_sda=p[9]; c.pin_sccb_scl=p[10];
    c.pin_vsync=p[11]; c.pin_href=p[12]; c.pin_pclk=p[13];
    return c;
}
// Board-specific GPIO preparation belongs to the sketch too.
inline void prepare(Board board) {
    if (board == Board::EspEye) {
        pinMode(13, INPUT_PULLUP); pinMode(14, INPUT_PULLUP);
    }
}
}
