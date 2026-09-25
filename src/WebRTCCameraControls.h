#pragma once
#include <Arduino.h>
#include <esp_camera.h>

// Camera settings a viewer can change over the DataChannel, plus automatic quality adaptation.
// Control messages are UTF-8 JSON text; binary messages carry JPEG fragments.
//   device -> viewer  {"type":"capabilities","resolutions":["QVGA","VGA","SVGA"],"minFps":1,"maxFps":15,
//                      "flash":true,"flip":true,"mirror":true,"brightness":true,"contrast":true,
//                      "aeLevel":true,"minImageLevel":-2,"maxImageLevel":2}
//   device -> viewer  {"type":"state","resolution":"VGA","fps":5,"flash":false,"flip":false,"mirror":false,
//                      "brightness":0,"contrast":0,"aeLevel":0,
//                      "autoQuality":true,"qualityLevel":0,"effectiveFps":5.0}
//   viewer -> device  {"type":"set", ...any of resolution, fps, flash, flip, mirror, brightness,
//                      contrast, aeLevel, autoQuality}
// Not thread-safe: use it from the task that owns the peer.
class WebRTCCameraControls {
public:
    static constexpr int kMinFps = 1;
    static constexpr int kMaxFps = 15;
    // Range esp32-camera accepts for brightness, contrast and auto-exposure level.
    static constexpr int kMinImageLevel = -2;
    static constexpr int kMaxImageLevel = 2;

    // Call after esp_camera_init(). maxFrameSize must not exceed the size the camera was
    // initialized with (its JPEG buffers are sized for that); FRAMESIZE_INVALID = current size.
    void begin(framesize_t maxFrameSize, int flashPin, uint32_t frameIntervalMs, bool autoQuality);

    // Switches the reported video path between the H.264 track and DataChannel JPEG.
    // `resolution` is the encoder's fixed resolution, e.g. "QVGA".
    void setH264(bool active, const char *resolution, int fps);

    // Applies a "set" message; returns true if anything changed.
    bool apply(const String &message);

    // Feeds automatic quality with the outcome of each streamed frame.
    void onFrameResult(bool completed, uint32_t durationMs);

    // Restores full quality and turns the flash off when the viewer disconnects.
    void viewerLeft();

    // Call when a DataChannel viewer connects. A weak link starts at a reduced quality level, from
    // which automatic quality can climb, and cannot select sizes above VGA for this session.
    void startSession(bool weakLink);

    // Re-applies the remembered sensor settings after the camera has been re-initialised.
    void reapply(bool includeFrameSize);

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
    bool setImageLevel(int &current, int level, int (*setter)(sensor_t *, int));
    void setLevel(uint8_t level);
    framesize_t selectableMax() const;

    framesize_t frameSize_ = FRAMESIZE_VGA;
    framesize_t maxFrameSize_ = FRAMESIZE_VGA;
    bool weakLink_ = false;
    // An H.264 session runs at the one resolution the encoder was created for, so the resolution
    // control is reported as a single choice and changes are rejected.
    bool h264Active_ = false;
    const char *h264Resolution_ = nullptr;
    int flashPin_ = -1;
    int fps_ = 5;
    int baseQuality_ = 12;
    bool flash_ = false;
    bool flip_ = false;
    bool mirror_ = false;
    int brightness_ = 0;
    int contrast_ = 0;
    int aeLevel_ = 0;
    bool autoQuality_ = true;
    uint8_t level_ = 0;
    uint8_t congestedFrames_ = 0;
    uint8_t goodFrames_ = 0;
    bool stateChanged_ = false;
};
