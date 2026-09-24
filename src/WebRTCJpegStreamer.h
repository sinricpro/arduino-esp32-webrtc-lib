#pragma once
#include <Arduino.h>
#include <algorithm>
#include <esp_camera.h>
#include "SinricProWebRTC.h"

// Streams esp_camera JPEG frames over a WebRTC DataChannel in bounded fragments.
// Wire format per message: four little-endian uint32 (magic 0x47504A53, frame id, total length,
// offset) followed by up to kChunkSize bytes. Viewers reassemble by frame id and offset.
// Call loop() from the task that owns the peer; each call sends at most kBurstFragments and
// returns so ICE/SCTP processing keeps running between them.
class WebRTCJpegStreamer {
public:
    static constexpr uint32_t kMagic = 0x47504A53;
    static constexpr size_t kHeaderSize = 16;
    static constexpr size_t kChunkSize = 1024;
    // A frame is abandoned when no fragment has gone out for kStallMs, or after kMaxFrameMs in all.
    // Timing the whole frame against 1 s discarded every frame on a slow but working link: at
    // -78 dBm about 6 kB/s moves, and a frame is larger than that.
    static constexpr uint32_t kStallMs = 1000;
    static constexpr uint32_t kMaxFrameMs = 5000;
    // Fragments per call. One is slow but survives a weak link: at -81 dBm even four at once ran
    // classic ESP32's Wi-Fi TX buffers out (sendto ENOMEM) and wedged the session for good.
    static constexpr size_t kBurstFragments = 1;

    enum class Result { Idle, Sending, Completed, Abandoned };

    void configure(uint32_t frameIntervalMs, size_t maxFrameBytes) {
        frameIntervalMs_ = frameIntervalMs;
        maxFrameBytes_ = maxFrameBytes;
    }

    void setFrameInterval(uint32_t frameIntervalMs) { frameIntervalMs_ = frameIntervalMs; }

    // Time from capture to the last fragment (or to abandonment) of the most recent finished frame.
    uint32_t lastFrameDurationMs() const { return lastFrameDurationMs_; }

    // Outcome of the most recent finished frame. Many blocked sends with little progress means the
    // data channel is wedged rather than merely slow, which no amount of quality reduction fixes.
    uint32_t lastBlockedSends() const { return lastBlockedSends_; }
    size_t lastSentBytes() const { return lastSentBytes_; }
    size_t lastFrameBytes() const { return lastFrameBytes_; }

    Result loop(SinricProWebRTC &rtc, uint16_t channel) {
        if (!frame_) {
            if (millis() - lastFrame_ < frameIntervalMs_)
                return Result::Idle;
            lastFrame_ = millis();
            frame_ = esp_camera_fb_get();
            if (!frame_)
                return Result::Idle;
            frameStarted_ = lastProgress_ = millis();
            // Oversized frames count as congestion so automatic quality can compress harder.
            if (frame_->format != PIXFORMAT_JPEG || frame_->len > maxFrameBytes_)
                return finish(Result::Abandoned);
            offset_ = 0;
            ++frameId_;
        }

        uint8_t packet[kHeaderSize + kChunkSize];
        int ret = 0;
        for (size_t burst = 0; burst < kBurstFragments && offset_ < frame_->len; ++burst) {
            uint32_t header[] = {kMagic, frameId_, static_cast<uint32_t>(frame_->len), static_cast<uint32_t>(offset_)};
            memcpy(packet, header, sizeof(header));
            size_t bytes = std::min(kChunkSize, frame_->len - offset_);
            memcpy(packet + kHeaderSize, frame_->buf + offset_, bytes);

            ret = rtc.sendBinary(channel, packet, bytes + kHeaderSize);
            if (ret != 0)
                break;
            offset_ += bytes;
            lastProgress_ = millis();
        }
        if (ret == ESP_PEER_ERR_WOULD_BLOCK)
            ++blockedSends_;
        if (offset_ == frame_->len)
            return finish(Result::Completed);
        // Abandon frames that stall on a congested link so the viewer gets a fresh one instead.
        const uint32_t now = millis();
        if (now - lastProgress_ > kStallMs || now - frameStarted_ > kMaxFrameMs ||
            (ret && ret != ESP_PEER_ERR_WOULD_BLOCK))
            return finish(Result::Abandoned);
        return Result::Sending;
    }

    void reset() {
        if (frame_)
            esp_camera_fb_return(frame_);
        frame_ = nullptr;
        offset_ = 0;
    }

private:
    Result finish(Result result) {
        lastFrameDurationMs_ = millis() - frameStarted_;
        lastBlockedSends_ = blockedSends_;
        lastSentBytes_ = offset_;
        lastFrameBytes_ = frame_ ? frame_->len : 0;
        blockedSends_ = 0;
        // reset() clears offset_ and the frame, so the counters above must be captured first.
        reset();
        return result;
    }

    camera_fb_t *frame_ = nullptr;
    size_t offset_ = 0;
    uint32_t frameId_ = 0;
    uint32_t lastFrame_ = 0;
    uint32_t frameStarted_ = 0;
    uint32_t lastProgress_ = 0;
    uint32_t lastFrameDurationMs_ = 0;
    uint32_t blockedSends_ = 0;
    uint32_t lastBlockedSends_ = 0;
    size_t lastSentBytes_ = 0;
    size_t lastFrameBytes_ = 0;
    uint32_t frameIntervalMs_ = 200;
    size_t maxFrameBytes_ = 128 * 1024;
};
