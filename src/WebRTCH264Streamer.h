#pragma once
#include <Arduino.h>
#include <esp_camera.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "SinricProWebRTC.h"

// H.264 video track for ESP32-S3, where esp_h264 encodes in software.
//
// The camera delivers YUV422 (YUYV), one of the two formats the software encoder accepts, so
// frames go from the camera buffer into the encoder untouched, and esp_peer packetises each
// encoded frame into RTP.
//
// A QVGA frame costs roughly 90 ms to encode, which is why this runs on its own task: the session
// task has to stay free for ICE, DTLS and the 20 ms audio pump. Encoded frames cross between the
// two tasks through a free/ready queue pair, so neither waits for the other.
//
// ESP32-S3 only: esp_h264 has no prebuilt library for classic ESP32, which keeps the JPEG path.
class WebRTCH264Streamer {
public:
    struct Config {
        uint16_t width = 320;
        uint16_t height = 240;
        uint8_t fps = 10;
        uint32_t bitrate = 400000;
        uint32_t taskStackSize = 12 * 1024;
        UBaseType_t taskPriority = 4;
    };

    WebRTCH264Streamer() = default;
    ~WebRTCH264Streamer() { end(); }
    WebRTCH264Streamer(const WebRTCH264Streamer &) = delete;
    WebRTCH264Streamer &operator=(const WebRTCH264Streamer &) = delete;

    // Creates the encoder and starts capturing. The camera must already be in YUV422 mode.
    bool begin(const Config &config);

    // Stops the encoder task before the caller reconfigures the camera: the task holds a frame
    // buffer while it encodes.
    void end();

    // Sends at most one encoded frame, so the caller keeps servicing the peer between frames.
    bool send(SinricProWebRTC &rtc);

    // Answers an RTCP PLI. The software encoder cannot force an IDR, so the encoder is recreated,
    // which emits a fresh IDR with SPS and PPS. Rate limited, since a viewer losing packets
    // repeats the request.
    void requestKeyframe() { keyframeRequested_ = true; }

    void setFps(uint8_t fps);

    bool running() const { return running_; }
    uint32_t encodedFrames() const { return encoded_; }
    uint32_t droppedFrames() const { return dropped_; }

private:
    struct Slot {
        uint8_t *data = nullptr;
        uint32_t capacity = 0;
        uint32_t size = 0;
        uint32_t pts = 0;
    };
    static constexpr size_t kSlotCount = 2;  // one being sent while the other is filled

    static void taskEntry(void *arg);
    void run();
    bool openEncoder();
    void closeEncoder();
    bool encodeFrame(Slot *slot, uint32_t now);
    uint8_t *alignedInput(const camera_fb_t *frame);
    void release();

    Config config_;
    // esp_h264 handles, kept opaque so this header pulls in no encoder types.
    void *encoder_ = nullptr;
    void *params_ = nullptr;

    TaskHandle_t task_ = nullptr;
    SemaphoreHandle_t stopped_ = nullptr;
    QueueHandle_t freeSlots_ = nullptr;
    QueueHandle_t readySlots_ = nullptr;
    Slot slots_[kSlotCount];
    // Used only when the camera hands back a buffer the encoder cannot read directly.
    uint8_t *staging_ = nullptr;
    uint32_t stagingSize_ = 0;

    volatile bool running_ = false;
    volatile bool keyframeRequested_ = false;
    volatile uint8_t fps_ = 10;
    uint32_t startedMs_ = 0;
    uint32_t lastFrameMs_ = 0;
    uint32_t lastKeyframeMs_ = 0;
    uint32_t encoded_ = 0;
    uint32_t dropped_ = 0;
    uint32_t lastLogMs_ = 0;
    uint32_t logFrames_ = 0;
    uint32_t logEncodeMs_ = 0;
};
