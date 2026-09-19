#include "WebRTCH264Streamer.h"

#if CONFIG_IDF_TARGET_ESP32S3

#include <cstring>
#include "esp_h264_enc_param.h"
#include "esp_h264_enc_single.h"
#include "esp_h264_enc_single_sw.h"

// Unlike the other esp_h264 headers this one carries no extern "C" guard, so without this its
// declarations would get C++ linkage and never match the C definitions in the archive.
#include "esp_heap_caps.h"
extern "C" {
#include "esp_h264_alloc.h"
}

namespace {
constexpr uint32_t kKeyframeMinIntervalMs = 2000;
constexpr uint32_t kStopPollMs = 50;
constexpr uint32_t kLogIntervalMs = 5000;
// Quantiser floor and ceiling. The floor stops a detailed scene spending the whole bitrate on one
// frame; the ceiling keeps a moving scene from turning to mush.
constexpr uint8_t kQpMin = 26;
constexpr uint8_t kQpMax = 40;
}  // namespace

bool WebRTCH264Streamer::begin(const Config &config) {
    if (task_)
        return true;
    if (!config.width || !config.height)
        return false;

    config_ = config;
    fps_ = config.fps ? config.fps : 10;
    startedMs_ = lastLogMs_ = millis();

    freeSlots_ = xQueueCreate(kSlotCount, sizeof(Slot *));
    readySlots_ = xQueueCreate(kSlotCount, sizeof(Slot *));
    stopped_ = xSemaphoreCreateBinary();
    if (!freeSlots_ || !readySlots_ || !stopped_) {
        release();
        return false;
    }

    // One byte per pixel holds an IDR comfortably at these resolutions, and P-frames use a
    // fraction of it. Both buffers live in PSRAM; only the RTP copy touches internal RAM.
    const uint32_t capacity = static_cast<uint32_t>(config_.width) * config_.height;
    for (Slot &slot : slots_) {
        slot.data = static_cast<uint8_t *>(
            esp_h264_aligned_calloc(16, 1, capacity, &slot.capacity, MALLOC_CAP_SPIRAM));
        if (!slot.data) {
            release();
            return false;
        }
        Slot *entry = &slot;
        xQueueSend(freeSlots_, &entry, 0);
    }

    if (!openEncoder()) {
        release();
        return false;
    }

    running_ = true;
    // Pinned away from core 0, where the Wi-Fi driver and the session task do their work.
    if (xTaskCreatePinnedToCore(taskEntry, "h264_enc", config_.taskStackSize, this, config_.taskPriority,
                                &task_, 1) != pdPASS) {
        running_ = false;
        release();
        return false;
    }

    log_i("H.264 %ux%u at %u fps, %u bps", config_.width, config_.height, fps_, config_.bitrate);
    return true;
}

void WebRTCH264Streamer::end() {
    if (!task_)
        return;
    running_ = false;
    // The task holds a camera frame buffer while encoding, so it must finish before the caller
    // reconfigures the camera.
    xSemaphoreTake(stopped_, pdMS_TO_TICKS(2 * kStopPollMs + 1000));
    task_ = nullptr;
    log_i("H.264 stopped after %u frames, %u dropped", encoded_, dropped_);
    release();
}

void WebRTCH264Streamer::release() {
    closeEncoder();
    for (Slot &slot : slots_) {
        if (slot.data)
            esp_h264_free(slot.data);
        slot.data = nullptr;
        slot.capacity = 0;
    }
    if (staging_)
        esp_h264_free(staging_);
    staging_ = nullptr;
    stagingSize_ = 0;
    if (freeSlots_)
        vQueueDelete(freeSlots_);
    if (readySlots_)
        vQueueDelete(readySlots_);
    if (stopped_)
        vSemaphoreDelete(stopped_);
    freeSlots_ = readySlots_ = nullptr;
    stopped_ = nullptr;
}

bool WebRTCH264Streamer::openEncoder() {
    const uint8_t fps = fps_ ? fps_ : 1;
    esp_h264_enc_cfg_sw_t cfg = {};
    cfg.pic_type = ESP_H264_RAW_FMT_YUYV;
    // One keyframe per second: a viewer that joins late or loses packets recovers quickly, which
    // matters because the software encoder cannot produce one on demand.
    cfg.gop = fps;
    cfg.fps = fps;
    cfg.res.width = config_.width;
    cfg.res.height = config_.height;
    cfg.rc.bitrate = config_.bitrate;
    cfg.rc.qp_min = kQpMin;
    cfg.rc.qp_max = kQpMax;

    esp_h264_enc_handle_t encoder = nullptr;
    if (esp_h264_enc_sw_new(&cfg, &encoder) != ESP_H264_ERR_OK || !encoder) {
        log_e("H.264 encoder allocation failed for %ux%u", config_.width, config_.height);
        return false;
    }
    encoder_ = encoder;

    esp_h264_enc_param_sw_handle_t params = nullptr;
    if (esp_h264_enc_sw_get_param_hd(encoder, &params) != ESP_H264_ERR_OK ||
        esp_h264_enc_open(encoder) != ESP_H264_ERR_OK) {
        log_e("H.264 encoder open failed");
        closeEncoder();
        return false;
    }
    params_ = params;
    lastKeyframeMs_ = millis();
    return true;
}

void WebRTCH264Streamer::closeEncoder() {
    if (!encoder_)
        return;
    auto encoder = static_cast<esp_h264_enc_handle_t>(encoder_);
    esp_h264_enc_close(encoder);
    esp_h264_enc_del(encoder);
    encoder_ = nullptr;
    params_ = nullptr;
}

// openh264 reads the input plane in 16-byte steps, so an unaligned camera buffer is copied.
uint8_t *WebRTCH264Streamer::alignedInput(const camera_fb_t *frame) {
    if ((reinterpret_cast<uintptr_t>(frame->buf) & 15) == 0)
        return frame->buf;
    if (!staging_ || stagingSize_ < frame->len) {
        if (staging_)
            esp_h264_free(staging_);
        stagingSize_ = 0;
        staging_ = static_cast<uint8_t *>(
            esp_h264_aligned_calloc(16, 1, frame->len, &stagingSize_, MALLOC_CAP_SPIRAM));
        if (!staging_)
            return nullptr;
    }
    memcpy(staging_, frame->buf, frame->len);
    return staging_;
}

bool WebRTCH264Streamer::encodeFrame(Slot *slot, uint32_t now) {
    camera_fb_t *frame = esp_camera_fb_get();
    if (!frame)
        return false;

    bool encoded = false;
    if (frame->format != PIXFORMAT_YUV422) {
        log_w("Camera is not in YUV422 mode");
    } else if (uint8_t *input = alignedInput(frame)) {
        esp_h264_enc_in_frame_t in = {};
        in.raw_data.buffer = input;
        in.raw_data.len = frame->len;
        in.pts = now - startedMs_;

        esp_h264_enc_out_frame_t out = {};
        out.raw_data.buffer = slot->data;
        out.raw_data.len = slot->capacity;

        const esp_h264_err_t err =
            esp_h264_enc_process(static_cast<esp_h264_enc_handle_t>(encoder_), &in, &out);
        if (err == ESP_H264_ERR_OK && out.length > 0 && out.length <= slot->capacity) {
            slot->size = out.length;
            slot->pts = in.pts;
            encoded = true;
        } else {
            log_w("H.264 encode failed: %d, %u bytes", static_cast<int>(err), out.length);
        }
    }

    esp_camera_fb_return(frame);
    return encoded;
}

void WebRTCH264Streamer::taskEntry(void *arg) {
    static_cast<WebRTCH264Streamer *>(arg)->run();
}

void WebRTCH264Streamer::run() {
    while (running_) {
        const uint8_t fps = fps_ ? fps_ : 1;
        const uint32_t interval = 1000 / fps;
        const uint32_t now = millis();
        if (now - lastFrameMs_ < interval) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        // A viewer that cannot keep up leaves both slots outstanding; the wait doubles as the
        // back pressure that keeps the encoder from running ahead of the link.
        Slot *slot = nullptr;
        if (xQueueReceive(freeSlots_, &slot, pdMS_TO_TICKS(kStopPollMs)) != pdTRUE)
            continue;
        lastFrameMs_ = now;

        if (keyframeRequested_) {
            keyframeRequested_ = false;
            if (now - lastKeyframeMs_ >= kKeyframeMinIntervalMs) {
                closeEncoder();
                if (!openEncoder())
                    running_ = false;
            }
        }

        if (encoder_ && encodeFrame(slot, now)) {
            encoded_++;
            xQueueSend(readySlots_, &slot, 0);
        } else {
            dropped_++;
            xQueueSend(freeSlots_, &slot, 0);
        }

        logFrames_++;
        logEncodeMs_ += millis() - now;
        if (now - lastLogMs_ >= kLogIntervalMs) {
            log_i("%u frames in %u ms, %u ms each, %u dropped so far", logFrames_, now - lastLogMs_,
                  logEncodeMs_ / logFrames_, dropped_);
            lastLogMs_ = now;
            logFrames_ = 0;
            logEncodeMs_ = 0;
        }

        // Encoding occupies this core for nearly the whole frame interval, leaving the pacing
        // delay above unreachable. Without an explicit tick the priority-0 idle task never runs
        // and the task watchdog kills the session after five seconds.
        vTaskDelay(1);
    }

    xSemaphoreGive(stopped_);
    vTaskDelete(nullptr);
}

bool WebRTCH264Streamer::send(SinricProWebRTC &rtc) {
    Slot *slot = nullptr;
    if (!readySlots_ || xQueueReceive(readySlots_, &slot, 0) != pdTRUE)
        return false;

    const int ret = rtc.sendVideo(slot->data, slot->size, slot->pts);
    if (ret != 0)
        dropped_++;
    xQueueSend(freeSlots_, &slot, 0);
    return ret == 0;
}

void WebRTCH264Streamer::setFps(uint8_t fps) {
    if (!fps || fps == fps_)
        return;
    fps_ = fps;
    // The encoder's rate control needs the same frame rate the capture loop uses, or it spends
    // the bitrate as though frames arrived faster than they do.
    if (params_) {
        auto params = static_cast<esp_h264_enc_param_handle_t>(params_);
        esp_h264_enc_set_fps(params, fps);
        esp_h264_enc_set_gop(params, fps);
    }
}

#else  // !CONFIG_IDF_TARGET_ESP32S3

// Classic ESP32 has no esp_h264 prebuilt library; sessions there stream JPEG over the DataChannel.
bool WebRTCH264Streamer::begin(const Config &) { return false; }
void WebRTCH264Streamer::end() {}
bool WebRTCH264Streamer::send(SinricProWebRTC &) { return false; }
void WebRTCH264Streamer::setFps(uint8_t) {}
void WebRTCH264Streamer::taskEntry(void *) {}
void WebRTCH264Streamer::run() {}
bool WebRTCH264Streamer::openEncoder() { return false; }
void WebRTCH264Streamer::closeEncoder() {}
bool WebRTCH264Streamer::encodeFrame(Slot *, uint32_t) { return false; }
uint8_t *WebRTCH264Streamer::alignedInput(const camera_fb_t *) { return nullptr; }
void WebRTCH264Streamer::release() {}

#endif
