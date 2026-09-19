#pragma once
#include <Arduino.h>
#include <atomic>
#include <functional>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "SinricProWebRTC.h"
#include "WebRTCCameraControls.h"
#include "WebRTCH264Streamer.h"
#include "WebRTCJpegStreamer.h"

struct WebRTCIceServer {
    String url;  // "stun:host:port", "turn:host:port?transport=udp", "turns:host:443?transport=tcp"
    String username;
    String credential;
};

// Answers viewer offers that arrive over a cloud signaling channel (SinricPro getWebRTCAnswer)
// and streams camera JPEG frames over the viewer's DataChannel, with viewer controls
// (WebRTCCameraControls) and an optional PCMU microphone track.
// One viewer at a time: a new offer replaces the current one. Every esp_peer call runs on the
// session task; handleOffer() exchanges data with it only through a queue and semaphores.
class SinricProWebRTCSession {
public:
    struct Config {
        uint32_t taskStackSize = 24 * 1024;
        UBaseType_t taskPriority = 4;
        uint32_t answerTimeoutMs = 4000;        // leaves headroom inside Alexa's 6 s answer budget
        uint32_t channelOpenTimeoutMs = 15000;  // drop viewers whose DataChannel never opens
        uint32_t frameIntervalMs = 200;         // initial frame rate; viewers can change it
        size_t maxFrameBytes = 128 * 1024;
        int dataChannelSendCache = 48 * 1024;
        int dataChannelRecvCache = 16 * 1024;
        bool autoQuality = true;                // lower frame rate, then JPEG quality, when the link backs up
        // Largest viewer-selectable resolution. Must not exceed the size esp_camera_init() used,
        // since the camera's JPEG buffers are sized for it. FRAMESIZE_INVALID = current size only.
        framesize_t maxFrameSize = FRAMESIZE_INVALID;
        int flashPin = -1;                      // flash LED GPIO (AI-Thinker ESP32-CAM: 4)
        bool audio = false;                     // send PCMU from setAudioSource() when the viewer offers audio

        // Send H.264 on a WebRTC video track when the viewer offers one (ESP32-S3 only, where
        // esp_h264 encodes in software). A viewer that offers no video track still gets JPEG over
        // the DataChannel, so older app and portal versions keep working.
        bool h264 = false;
        uint16_t h264Width = 320;
        uint16_t h264Height = 240;
        uint8_t h264Fps = 10;
        uint32_t h264Bitrate = 400000;
        // The board's camera wiring, as passed to esp_camera_init(). Required when h264 is set:
        // the session re-initialises the camera in YUV422 for a video track and back to JPEG after.
        camera_config_t cameraConfig = {};
    };

    // Fills `size` bytes of 8 kHz PCMU (160 = 20 ms) and returns true when a frame is ready.
    // Called on the session task every loop, even without a viewer so the source can drain its
    // input; must not block.
    using AudioSource = std::function<bool(uint8_t *pcmu, size_t size)>;

    SinricProWebRTCSession() = default;
    SinricProWebRTCSession(const SinricProWebRTCSession &) = delete;
    SinricProWebRTCSession &operator=(const SinricProWebRTCSession &) = delete;

    // Call after esp_camera_init(). Overloads rather than a default argument: Config's member
    // initializers are not usable until the enclosing class is complete.
    bool begin() { return begin(Config{}); }
    bool begin(const Config &config);

    // Set before begin().
    void setAudioSource(AudioSource source) { audioSource_ = source; }

    // Blocks until the local answer (with every gathered candidate) is ready or the timeout expires.
    bool handleOffer(const String &offerSdp, const std::vector<WebRTCIceServer> &iceServers, String &answerSdp);

    // Reason for the last handleOffer() failure, suitable for SinricPro.setResponseMessage().
    // Read it from the task that called handleOffer().
    String lastError() const { return lastError_; }

    // Closes the current viewer, if any.
    void stop();

    bool isStreaming() const { return channelOpen_; }

private:
    enum class CommandType : uint8_t { Start, Stop };

    struct Command {
        CommandType type;
        uint32_t sequence;
        char *offer;                               // owned; freed by the session task
        std::vector<WebRTCIceServer> *iceServers;  // owned; freed by the session task
    };

    static void taskEntry(void *arg);
    static int onMessage(esp_peer_msg_t *msg, void *ctx);
    static int onState(esp_peer_state_t state, void *ctx);
    static int onChannelOpen(esp_peer_data_channel_info_t *ch, void *ctx);
    static int onChannelClose(esp_peer_data_channel_info_t *ch, void *ctx);
    static int onData(esp_peer_data_frame_t *frame, void *ctx);
    static void freeCommand(Command &cmd);

    void run();
    void startPeer(Command &cmd);
    void closePeer();
    void publishAnswer(bool ok, const String &error = String());
    void pollAudio();
    void streamToViewer();
    bool selectCameraFormat(bool yuv);
    void startH264();
    void stopH264();
    String buildAnswer() const;

    Config config_;
    SinricProWebRTC rtc_;
    WebRTCJpegStreamer streamer_;
    WebRTCH264Streamer h264_;
    WebRTCCameraControls controls_;
    AudioSource audioSource_;
    esp_peer_default_cfg_t peerDefaults_ = {};
    QueueHandle_t commands_ = nullptr;
    SemaphoreHandle_t answerReady_ = nullptr;
    SemaphoreHandle_t answerLock_ = nullptr;
    SemaphoreHandle_t offerLock_ = nullptr;
    TaskHandle_t task_ = nullptr;

    // Owned by the handleOffer() caller.
    String lastError_;

    // Owned by the session task.
    std::vector<WebRTCIceServer> iceServers_;
    std::vector<esp_peer_ice_server_cfg_t> iceServerCfg_;
    String localSdp_;
    std::vector<String> localCandidates_;
    uint32_t activeSequence_ = 0;
    uint32_t sessionStarted_ = 0;
    uint32_t lastSignal_ = 0;
    uint32_t audioPts_ = 0;
    bool answerPublished_ = true;
    bool closeRequested_ = false;
    bool audioActive_ = false;
    bool videoActive_ = false;
    // The portal and the app carry their controls on a DataChannel; Alexa and Google Home offer
    // media only. Its absence is what marks a smart-display viewer.
    bool dataChannelOffered_ = false;
    bool capabilitiesPending_ = false;
    bool statePending_ = false;
    uint16_t channelId_ = 0;
    std::atomic<bool> channelOpen_{false};

    // Shared with handleOffer(); guarded by answerLock_.
    std::atomic<uint32_t> sequence_{0};
    uint32_t answerSequence_ = 0;
    bool answerOk_ = false;
    String answer_;
    String answerError_;
};
