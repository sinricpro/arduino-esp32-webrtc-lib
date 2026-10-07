#pragma once

#define WIFI_SSID  ""
#define WIFI_PASS  ""

// From https://portal.sinric.pro -> Credentials.
#define APP_KEY    ""
#define APP_SECRET ""

// Camera device with Camera Stream Configuration: Board "ESP32", Streaming Protocol "WebRTC",
// H.264 video track ticked.
#define CAMERA_ID  ""

// The camera sensor's I2C (SCCB) pins. GPIO7/GPIO8 on the Espressif ESP32-P4-Function-EV-Board and
// the Waveshare ESP32-P4 boards, where the bus is shared with the audio codec.
#define CAMERA_SCCB_SDA 7
#define CAMERA_SCCB_SCL 8

// H.264 size: 800 for 800x640 at 15 fps (a centre crop of the sensor), 1280 for the full field of
// view at 1280x960, about 11 fps. Smart displays always get 1280x960.
#define H264_WIDTH 800
