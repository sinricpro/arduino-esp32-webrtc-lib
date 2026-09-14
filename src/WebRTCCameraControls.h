#pragma once
#include <Arduino.h>
#include <esp_camera.h>

// Camera settings a viewer can change over the DataChannel, plus automatic quality adaptation.
// Control messages are UTF-8 JSON text; binary messages carry JPEG fragments.
//   device -> viewer  {"type":"capabilities","resolutions":["QVGA","VGA","SVGA"],"minFps":1,"maxFps":15,
//                      "flash":true,"flip":true,"mirror":true}
//   device -> viewer  {"type":"state","resolution":"VGA","fps":5,"flash":false,"flip":false,"mirror":false,
//                      "autoQuality":true,"qualityLevel":0,"effectiveFps":5.0}
//   viewer -> device  {"type":"set", ...any of resolution, fps, flash, flip, mirror, autoQuality}
// Not thread-safe: use it from the task that owns the peer.
class WebRTCCameraControls {
public:
    static constexpr int kMinFps = 1;
    static constexpr int kMaxFps = 15;

    // Call after esp_camera_init(). maxFrameSize must not exceed the size the camera was
    // initialized with (its JPEG buffers are sized for that); FRAMESIZE_INVALID = current size.
    void begin(framesize_t maxFrameSize, int flashPin, uint32_t frameIntervalMs, bool autoQuality);

    // Applies a "set" message; returns true if anything changed.
    bool apply(const String &message);

    // Feeds automatic quality with the outcome of each streamed frame.
    void onFrameResult(bool completed, uint32_t durationMs);

    // Restores full quality and turns the flash off when the viewer disconnects.
    void viewerLeft();

    uint32_t frameIntervalMs() const;
    String capabilitiesJson() const;
    String stateJson() const;

    // True once after any state change the viewer has not been told about.
    bool takeStateChanged();

private:
    bool setResolution(const String &name);
    bool setFps(int fps);
    bool setFlash(bool on);
    bool setFlip(bool on);
    bool setMirror(bool on);
    bool setAutoQuality(bool on);
    void setLevel(uint8_t level);

    framesize_t frameSize_ = FRAMESIZE_VGA;
    framesize_t maxFrameSize_ = FRAMESIZE_VGA;
    int flashPin_ = -1;
    int fps_ = 5;
    int baseQuality_ = 12;
    bool flash_ = false;
    bool flip_ = false;
    bool mirror_ = false;
    bool autoQuality_ = true;
    uint8_t level_ = 0;
    uint8_t congestedFrames_ = 0;
    uint8_t goodFrames_ = 0;
    bool stateChanged_ = false;
};
