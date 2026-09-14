#pragma once
#include <Arduino.h>
#include <esp_arduino_version.h>
#include "esp_peer.h"
#include "esp_peer_default.h"

#include "SinricProWebRTCVersion.h"
#if !CONFIG_IDF_TARGET_ESP32 && !CONFIG_IDF_TARGET_ESP32S3
#error "Precompiled WebRTC archives are available for ESP32 and ESP32-S3 only."
#endif

// All methods and callbacks run on the caller's task. Call loop() frequently.
// Do not call begin/end from a callback; defer lifecycle changes to your loop.
class SinricProWebRTC {
public:
    SinricProWebRTC() = default;
    ~SinricProWebRTC() { end(); }
    SinricProWebRTC(const SinricProWebRTC&) = delete;
    SinricProWebRTC& operator=(const SinricProWebRTC&) = delete;
    // Config and callback context must remain valid until end(). Default extra
    // config is supplied when extra_cfg is null. WiFi must already be connected.
    int begin(esp_peer_cfg_t config);
    void end();
    int loop();
    int startConnection();
    int signal(esp_peer_msg_type_t type, const uint8_t *data, size_t size);
    int sendText(uint16_t channel, const char *text);
    int sendBinary(uint16_t channel, const uint8_t *data, size_t size);
    int sendAudio(const uint8_t *g711, size_t size, uint32_t timestampMs);
    int sendVideo(const uint8_t *encoded, size_t size, uint32_t timestampMs);
    esp_peer_handle_t handle() const { return peer_; }
    static uint8_t encodeMuLaw(int16_t pcm);
private:
    esp_peer_handle_t peer_ = nullptr;
    esp_peer_default_cfg_t defaults_ = {};
};
