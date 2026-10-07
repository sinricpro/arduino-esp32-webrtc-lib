#pragma once
#include <Arduino.h>
#include <esp_camera.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "SinricProWebRTC.h"

// H.264 video track: esp_h264 encodes in software on the ESP32-S3 and in hardware on the ESP32-P4.
//
// On the S3 the camera delivers YUV422 (YUYV), one of the two formats the software encoder accepts,
// so frames go from the camera buffer into the encoder untouched. On the P4 the camera delivers
// BGR888 and the Pixel Processing Accelerator converts it to the packed YUV420 the hardware encoder
// takes. esp_peer packetises each encoded frame into RTP.
//
// A QVGA frame costs roughly 90 ms to encode on the S3, which is why this runs on its own task: the
// session task has to stay free for ICE, DTLS and the 20 ms audio pump. Encoded frames cross between
// the two tasks through a free/ready queue pair, so neither waits for the other.
//
// Classic ESP32 has no H.264 encoder and keeps the JPEG path.
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

    // Creates the encoder and starts capturing. The camera must already be in the encoder's input
    // format: YUV422 on the S3, RGB888 (BGR byte order) on the P4.
    bool begin(const Config &config);

    // Stops the encoder task before the caller reconfigures the camera: the task holds a frame
    // buffer while it encodes.
    void end();

    // Sends at most one encoded frame, so the caller keeps servicing the peer between frames.
    bool send(SinricProWebRTC &rtc);

    // Answers an RTCP PLI. The encoder offers no forced IDR here, so it is recreated,
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
    uint8_t *convertInput(const camera_fb_t *frame, uint32_t *length);
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
    // Encoder input: the PPA's YUV420 output on the P4, an aligned copy of an unaligned camera
    // buffer on the S3.
    uint8_t *staging_ = nullptr;
    uint32_t stagingSize_ = 0;
    void *ppa_ = nullptr;  // ppa_client_handle_t on the P4

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
