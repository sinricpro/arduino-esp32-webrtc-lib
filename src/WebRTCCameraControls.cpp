#include "WebRTCCameraControls.h"
#include <algorithm>
#include <ctype.h>

namespace {
struct ResolutionName {
    framesize_t size;
    const char *name;
};

const ResolutionName kResolutions[] = {
    {FRAMESIZE_QVGA, "QVGA"}, {FRAMESIZE_CIF, "CIF"}, {FRAMESIZE_VGA, "VGA"},   {FRAMESIZE_SVGA, "SVGA"},
    {FRAMESIZE_XGA, "XGA"},   {FRAMESIZE_HD, "HD"},   {FRAMESIZE_SXGA, "SXGA"}, {FRAMESIZE_UXGA, "UXGA"},
};

struct QualityLevel {
    uint16_t intervalPercent;
    uint8_t qualityOffset;
};

// Frame rate drops first because it keeps detail; JPEG compression follows when rate alone is not enough.
const QualityLevel kLevels[] = {{100, 0}, {150, 0}, {200, 6}, {300, 12}, {400, 18}, {600, 24}};
constexpr uint8_t kLevelCount = sizeof(kLevels) / sizeof(kLevels[0]);
constexpr uint8_t kDegradeAfterFrames = 2;
constexpr uint8_t kRecoverAfterFrames = 20;
constexpr int kMaxJpegQuality = 63;  // esp32-camera: higher number = stronger compression

// Minimal lookups for the flat JSON objects the SinricPro viewers send.
int valueStart(const String &json, const char *key) {
    String needle = String('"') + key + '"';
    int i = json.indexOf(needle);
    if (i < 0)
        return -1;
    i = json.indexOf(':', i + needle.length());
    if (i < 0)
        return -1;
    for (++i; i < static_cast<int>(json.length()) && isspace(static_cast<unsigned char>(json[i])); ++i) {
    }
    return i < static_cast<int>(json.length()) ? i : -1;
}

bool jsonString(const String &json, const char *key, String &out) {
    int i = valueStart(json, key);
    if (i < 0 || json[i] != '"')
        return false;
    int end = json.indexOf('"', i + 1);
    if (end < 0)
        return false;
    out = json.substring(i + 1, end);
    return true;
}

bool jsonInt(const String &json, const char *key, int &out) {
    int i = valueStart(json, key);
    if (i < 0 || !(isdigit(static_cast<unsigned char>(json[i])) || json[i] == '-'))
        return false;
    out = json.substring(i).toInt();
    return true;
}

bool jsonBool(const String &json, const char *key, bool &out) {
    int i = valueStart(json, key);
    if (i < 0)
        return false;
    if (json.startsWith("true", i)) {
        out = true;
        return true;
    }
    if (json.startsWith("false", i)) {
        out = false;
        return true;
    }
    return false;
}

const char *resolutionName(framesize_t size) {
    for (const ResolutionName &resolution : kResolutions) {
        if (resolution.size == size)
            return resolution.name;
    }
    return "";
}

const char *boolText(bool value) {
    return value ? "true" : "false";
}
}  // namespace

void WebRTCCameraControls::begin(framesize_t maxFrameSize, int flashPin, uint32_t frameIntervalMs, bool autoQuality) {
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        frameSize_ = sensor->status.framesize;
        baseQuality_ = sensor->status.quality;
        flip_ = sensor->status.vflip;
        mirror_ = sensor->status.hmirror;
    }
    maxFrameSize_ = maxFrameSize == FRAMESIZE_INVALID ? frameSize_ : maxFrameSize;

    flashPin_ = flashPin;
    if (flashPin_ >= 0) {
        pinMode(flashPin_, OUTPUT);
        digitalWrite(flashPin_, LOW);
    }

    fps_ = constrain(static_cast<int>(1000 / std::max<uint32_t>(frameIntervalMs, 1)), kMinFps, kMaxFps);
    autoQuality_ = autoQuality;
}

void WebRTCCameraControls::setH264(bool active, const char *resolution, int fps) {
    h264Active_ = active;
    h264Resolution_ = resolution;
    if (active) {
        // The JPEG quality ladder has nothing to act on while the encoder owns the bitrate.
        setLevel(0);
        fps_ = constrain(fps, kMinFps, kMaxFps);
    }
    stateChanged_ = true;
}

bool WebRTCCameraControls::apply(const String &message) {
    String type;
    if (!jsonString(message, "type", type) || type != "set")
        return false;

    bool changed = false;
    String text;
    int number;
    bool flag;
    if (jsonString(message, "resolution", text))
        changed |= setResolution(text);
    if (jsonInt(message, "fps", number))
        changed |= setFps(number);
    if (jsonBool(message, "flash", flag))
        changed |= setFlash(flag);
    if (jsonBool(message, "flip", flag))
        changed |= setFlip(flag);
    if (jsonBool(message, "mirror", flag))
        changed |= setMirror(flag);
    if (jsonBool(message, "autoQuality", flag))
        changed |= setAutoQuality(flag);

    // Report state even when a value was rejected so the viewer resyncs its controls.
    stateChanged_ = true;
    return changed;
}

void WebRTCCameraControls::onFrameResult(bool completed, uint32_t durationMs) {
    if (!autoQuality_)
        return;

    const uint32_t interval = frameIntervalMs();
    // A frame that takes longer to send than the frame interval means the link cannot keep up.
    if (!completed || durationMs > interval) {
        goodFrames_ = 0;
        if (++congestedFrames_ >= kDegradeAfterFrames && level_ + 1 < kLevelCount)
            setLevel(level_ + 1);
    } else if (durationMs * 2 < interval) {
        congestedFrames_ = 0;
        if (++goodFrames_ >= kRecoverAfterFrames && level_ > 0)
            setLevel(level_ - 1);
    } else {
        congestedFrames_ = 0;
        goodFrames_ = 0;
    }
}

void WebRTCCameraControls::viewerLeft() {
    setFlash(false);
    if (level_ > 0)
        setLevel(0);
    stateChanged_ = false;
}

uint32_t WebRTCCameraControls::frameIntervalMs() const {
    return (1000 / fps_) * kLevels[level_].intervalPercent / 100;
}

void WebRTCCameraControls::reapply(bool includeFrameSize) {
    sensor_t *sensor = esp_camera_sensor_get();
    if (!sensor)
        return;
    if (includeFrameSize)
        sensor->set_framesize(sensor, frameSize_);
    sensor->set_quality(sensor, std::min(kMaxJpegQuality, baseQuality_ + kLevels[level_].qualityOffset));
    sensor->set_vflip(sensor, flip_);
    sensor->set_hmirror(sensor, mirror_);
}

String WebRTCCameraControls::capabilitiesJson() const {
    String json = "{\"type\":\"capabilities\",\"resolutions\":[";
    if (h264Active_) {
        if (h264Resolution_) {
            json += '"';
            json += h264Resolution_;
            json += '"';
        }
    } else {
        bool first = true;
        for (const ResolutionName &resolution : kResolutions) {
            if (resolution.size > maxFrameSize_)
                continue;
            if (!first)
                json += ',';
            json += '"';
            json += resolution.name;
            json += '"';
            first = false;
        }
    }
    json += "],\"videoCodec\":\"";
    json += h264Active_ ? "h264" : "jpeg";
    json += "\",\"minFps\":";
    json += kMinFps;
    json += ",\"maxFps\":";
    json += kMaxFps;
    json += ",\"flash\":";
    json += boolText(flashPin_ >= 0);
    json += ",\"flip\":true,\"mirror\":true}";
    return json;
}

String WebRTCCameraControls::stateJson() const {
    String json = "{\"type\":\"state\",\"videoCodec\":\"";
    json += h264Active_ ? "h264" : "jpeg";
    json += "\",\"resolution\":\"";
    json += (h264Active_ && h264Resolution_) ? h264Resolution_ : resolutionName(frameSize_);
    json += "\",\"fps\":";
    json += fps_;
    json += ",\"flash\":";
    json += boolText(flash_);
    json += ",\"flip\":";
    json += boolText(flip_);
    json += ",\"mirror\":";
    json += boolText(mirror_);
    json += ",\"autoQuality\":";
    json += boolText(autoQuality_);
    json += ",\"qualityLevel\":";
    json += level_;
    json += ",\"effectiveFps\":";
    json += String(1000.0f / frameIntervalMs(), 1);
    json += '}';
    return json;
}

bool WebRTCCameraControls::takeStateChanged() {
    bool changed = stateChanged_;
    stateChanged_ = false;
    return changed;
}

bool WebRTCCameraControls::setResolution(const String &name) {
    if (h264Active_)
        return false;
    sensor_t *sensor = esp_camera_sensor_get();
    for (const ResolutionName &resolution : kResolutions) {
        if (name != resolution.name || resolution.size > maxFrameSize_)
            continue;
        if (resolution.size == frameSize_ || !sensor || sensor->set_framesize(sensor, resolution.size) != 0)
            return false;
        frameSize_ = resolution.size;
        setLevel(0);
        return true;
    }
    return false;
}

bool WebRTCCameraControls::setFps(int fps) {
    fps = constrain(fps, kMinFps, kMaxFps);
    if (fps == fps_)
        return false;
    fps_ = fps;
    setLevel(0);
    return true;
}

bool WebRTCCameraControls::setFlash(bool on) {
    if (flashPin_ < 0 || on == flash_)
        return false;
    digitalWrite(flashPin_, on ? HIGH : LOW);
    flash_ = on;
    stateChanged_ = true;
    return true;
}

bool WebRTCCameraControls::setFlip(bool on) {
    sensor_t *sensor = esp_camera_sensor_get();
    if (on == flip_ || !sensor || sensor->set_vflip(sensor, on) != 0)
        return false;
    flip_ = on;
    return true;
}

bool WebRTCCameraControls::setMirror(bool on) {
    sensor_t *sensor = esp_camera_sensor_get();
    if (on == mirror_ || !sensor || sensor->set_hmirror(sensor, on) != 0)
        return false;
    mirror_ = on;
    return true;
}

bool WebRTCCameraControls::setAutoQuality(bool on) {
    if (on == autoQuality_)
        return false;
    autoQuality_ = on;
    if (!on)
        setLevel(0);
    return true;
}

void WebRTCCameraControls::setLevel(uint8_t level) {
    congestedFrames_ = 0;
    goodFrames_ = 0;
    if (level == level_)
        return;
    level_ = level;

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor)
        sensor->set_quality(sensor, std::min(kMaxJpegQuality, baseQuality_ + kLevels[level_].qualityOffset));
    stateChanged_ = true;
}
