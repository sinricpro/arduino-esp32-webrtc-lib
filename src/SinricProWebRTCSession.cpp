#include "SinricProWebRTCSession.h"
#include <WiFi.h>
#include <algorithm>
#include <cstring>

namespace {
constexpr size_t kMaxSignalBytes = 16 * 1024;
constexpr size_t kMaxIceServers = 8;
// esp_peer reports its SDP after gathering; stray CANDIDATE messages may trail it briefly.
constexpr uint32_t kCandidateSettleMs = 200;
// Extra wait beyond answerTimeoutMs so the session task can report its own timeout first.
constexpr uint32_t kAnswerWaitSlackMs = 500;
constexpr size_t kMaxControlBytes = 512;
// Measured on an AI-Thinker ESP32-CAM: at -85 dBm the Wi-Fi TX buffers never recycle fast enough
// and the DTLS handshake cannot complete. -75 leaves margin before that cliff.
constexpr int32_t kWeakSignalDbm = -75;
constexpr size_t kAudioFrameBytes = 160;  // 20 ms of 8 kHz PCMU
constexpr uint32_t kAudioFrameMs = 20;

// A viewer that can render a video track offers H.264. Older viewers offer no video at all and
// keep the JPEG path, which is what makes this backward compatible.
bool offerWantsH264(const char *offer) {
    return strstr(offer, "m=video") != nullptr && strstr(offer, "H264") != nullptr;
}

framesize_t h264FrameSize(uint16_t width, const char **name) {
    if (width <= 160) {
        *name = "QQVGA";
        return FRAMESIZE_QQVGA;
    }
    if (width <= 320) {
        *name = "QVGA";
        return FRAMESIZE_QVGA;
    }
    // Alexa and Google Home both refuse anything below 480p, so VGA is the smallest size that can
    // reach them; the software encoder manages it only at a low frame rate.
    *name = "VGA";
    return FRAMESIZE_VGA;
}
}  // namespace

bool SinricProWebRTCSession::begin(const Config &config) {
    if (task_)
        return true;

    config_ = config;
    streamer_.configure(config_.frameIntervalMs, config_.maxFrameBytes);
    controls_.begin(config_.maxFrameSize, config_.flashPin, config_.frameIntervalMs, config_.autoQuality);

    commands_ = xQueueCreate(4, sizeof(Command));
    answerReady_ = xSemaphoreCreateBinary();
    answerLock_ = xSemaphoreCreateMutex();
    offerLock_ = xSemaphoreCreateMutex();
    if (!commands_ || !answerReady_ || !answerLock_ || !offerLock_)
        return false;

    // Pinned to core 0, beside WiFi and TLS, so core 1 belongs to the H.264 encoder alone. Left
    // unpinned it lands on core 1 too, and two busy tasks of equal priority there keep the idle
    // task off the core entirely, which trips the task watchdog and slows the encoder.
    return xTaskCreatePinnedToCore(taskEntry, "webrtc", config_.taskStackSize, this,
                                   config_.taskPriority, &task_, 0) == pdPASS;
}

bool SinricProWebRTCSession::handleOffer(const String &offerSdp, const std::vector<WebRTCIceServer> &iceServers,
                                         String &answerSdp) {
    answerSdp = "";
    if (!task_) {
        lastError_ = "WebRTC session not started";
        return false;
    }
    if (!offerSdp.startsWith("v=0") || offerSdp.length() > kMaxSignalBytes) {
        lastError_ = "Invalid WebRTC offer";
        return false;
    }

    xSemaphoreTake(offerLock_, portMAX_DELAY);
    lastError_ = "";

    Command cmd = {CommandType::Start, ++sequence_, strdup(offerSdp.c_str()),
                   new (std::nothrow) std::vector<WebRTCIceServer>(iceServers)};
    // Drop a completion left over from an earlier offer that timed out on this side.
    xSemaphoreTake(answerReady_, 0);

    if (!cmd.offer || !cmd.iceServers || xQueueSend(commands_, &cmd, 0) != pdTRUE) {
        lastError_ = (!cmd.offer || !cmd.iceServers) ? "Camera is out of memory" : "Camera WebRTC session is busy";
        freeCommand(cmd);
        xSemaphoreGive(offerLock_);
        return false;
    }

    bool answered = false;
    bool ok = false;
    const uint32_t deadline = millis() + config_.answerTimeoutMs + kAnswerWaitSlackMs;

    while (true) {
        int32_t remaining = static_cast<int32_t>(deadline - millis());
        if (remaining <= 0 || xSemaphoreTake(answerReady_, pdMS_TO_TICKS(remaining)) != pdTRUE)
            break;

        xSemaphoreTake(answerLock_, portMAX_DELAY);
        answered = answerSequence_ == cmd.sequence;
        if (answered) {
            ok = answerOk_;
            answerSdp = answer_;
            if (!ok)
                lastError_ = answerError_;
        }
        xSemaphoreGive(answerLock_);
        if (answered)
            break;
    }

    if (!answered)
        lastError_ = "Camera timed out creating the WebRTC answer";
    else if (ok && answerSdp.length() == 0)
        lastError_ = "Camera produced an empty WebRTC answer";

    const bool succeeded = ok && answerSdp.length() > 0;
    // The viewer sees this string, and a weak link is the usual cause: below about -80 dBm the
    // Wi-Fi driver runs out of TX buffers and the DTLS handshake never completes. Naming the
    // signal turns an opaque timeout into something the user can act on.
    if (!succeeded) {
        const int32_t rssi = WiFi.RSSI();
        if (rssi < kWeakSignalDbm)
            lastError_ += " (Wi-Fi signal " + String(rssi) + " dBm is too weak)";
    }

    xSemaphoreGive(offerLock_);
    return succeeded;
}

void SinricProWebRTCSession::stop() {
    if (!task_)
        return;
    Command cmd = {CommandType::Stop, 0, nullptr, nullptr};
    xQueueSend(commands_, &cmd, 0);
}

void SinricProWebRTCSession::taskEntry(void *arg) {
    static_cast<SinricProWebRTCSession *>(arg)->run();
}

void SinricProWebRTCSession::run() {
    for (;;) {
        Command cmd;
        while (xQueueReceive(commands_, &cmd, 0) == pdTRUE) {
            if (cmd.type == CommandType::Start)
                startPeer(cmd);
            else
                closePeer();
            freeCommand(cmd);
        }

        pollAudio();

        if (rtc_.handle()) {
            rtc_.loop();

            // Independent of the DataChannel: video flows as soon as the peer is connected.
            if (h264_.running())
                h264_.send(rtc_);

            const uint32_t now = millis();
            if (!answerPublished_) {
                if (localSdp_.length() && now - lastSignal_ >= kCandidateSettleMs) {
                    publishAnswer(true);
                } else if (now - sessionStarted_ >= config_.answerTimeoutMs) {
                    log_w("No local SDP within %u ms", config_.answerTimeoutMs);
                    publishAnswer(false, "Camera timed out gathering network candidates");
                    closeRequested_ = true;
                }
            }

            if (channelOpen_)
                streamToViewer();
            // Only a viewer that asked for a DataChannel is expected to open one. Alexa and
            // Google Home never do, and dropping their session here cut the stream off mid-play.
            // A session with neither a channel nor a video track has nothing to send, so that
            // one still times out.
            else if (answerPublished_ && (dataChannelOffered_ || !videoActive_) &&
                     now - sessionStarted_ > config_.channelOpenTimeoutMs)
                closeRequested_ = true;

            if (closeRequested_)
                closePeer();
        }

        vTaskDelay(1);
    }
}

void SinricProWebRTCSession::streamToViewer() {
    // Control messages go first: they are tiny and the viewer needs them to render its controls.
    if (capabilitiesPending_ && rtc_.sendText(channelId_, controls_.capabilitiesJson().c_str()) == 0)
        capabilitiesPending_ = false;
    if (!capabilitiesPending_ && statePending_ && rtc_.sendText(channelId_, controls_.stateJson().c_str()) == 0)
        statePending_ = false;

    if (videoActive_) {
        // The track carries the video; the DataChannel is left to the control protocol.
        h264_.setFps(static_cast<uint8_t>(1000 / std::max<uint32_t>(controls_.frameIntervalMs(), 1)));
        if (controls_.takeStateChanged())
            statePending_ = true;
        return;
    }

    streamer_.setFrameInterval(controls_.frameIntervalMs());
    WebRTCJpegStreamer::Result result = streamer_.loop(rtc_, channelId_);
    if (result == WebRTCJpegStreamer::Result::Completed || result == WebRTCJpegStreamer::Result::Abandoned) {
        if (result == WebRTCJpegStreamer::Result::Abandoned)
            log_w("Frame dropped: %u/%u bytes in %u ms, %u blocked sends",
                  static_cast<unsigned>(streamer_.lastSentBytes()), static_cast<unsigned>(streamer_.lastFrameBytes()),
                  static_cast<unsigned>(streamer_.lastFrameDurationMs()),
                  static_cast<unsigned>(streamer_.lastBlockedSends()));
        controls_.onFrameResult(result == WebRTCJpegStreamer::Result::Completed, streamer_.lastFrameDurationMs());
    }

    if (controls_.takeStateChanged())
        statePending_ = true;
}

void SinricProWebRTCSession::pollAudio() {
    if (!audioSource_)
        return;

    uint8_t pcmu[kAudioFrameBytes];
    if (!audioSource_(pcmu, sizeof(pcmu)))
        return;
    if (audioActive_ && channelOpen_)
        rtc_.sendAudio(pcmu, sizeof(pcmu), audioPts_);
    audioPts_ += kAudioFrameMs;
}

// The encoder reads YUV422 and the JPEG path needs JPEG, so the camera is re-initialised for the
// session and restored afterwards. Re-initialising resets the sensor, hence the reapply.
bool SinricProWebRTCSession::selectCameraFormat(bool yuv) {
    camera_config_t cfg = config_.cameraConfig;
    if (!cfg.xclk_freq_hz) {
        log_e("Config::cameraConfig is required for the H.264 video track");
        return false;
    }

    const char *name = nullptr;
    if (yuv) {
        cfg.pixel_format = PIXFORMAT_YUV422;
        cfg.frame_size = h264FrameSize(config_.h264Width, &name);
        cfg.fb_count = 2;
        cfg.grab_mode = CAMERA_GRAB_LATEST;
    }

    const esp_err_t err = esp_camera_reconfigure(&cfg);
    if (err != ESP_OK) {
        log_e("Camera reconfigure failed: 0x%x", err);
        return false;
    }
    controls_.reapply(!yuv);
    controls_.setH264(yuv, name, config_.h264Fps);
    return true;
}

void SinricProWebRTCSession::startH264() {
    WebRTCH264Streamer::Config cfg;
    cfg.width = config_.h264Width;
    cfg.height = config_.h264Height;
    cfg.fps = config_.h264Fps;
    cfg.bitrate = config_.h264Bitrate;
    cfg.taskPriority = config_.taskPriority;
    if (!h264_.begin(cfg)) {
        log_e("H.264 encoder failed to start");
        closeRequested_ = true;
    }
}

void SinricProWebRTCSession::stopH264() {
    h264_.end();
    if (videoActive_) {
        videoActive_ = false;
        selectCameraFormat(false);
    }
}

void SinricProWebRTCSession::startPeer(Command &cmd) {
    closePeer();

    activeSequence_ = cmd.sequence;
    answerPublished_ = false;
    sessionStarted_ = lastSignal_ = millis();
    localSdp_ = "";
    localCandidates_.clear();

    // esp_peer keeps pointers to these strings until end(), so they live in members.
    iceServers_ = *cmd.iceServers;
    if (iceServers_.size() > kMaxIceServers)
        iceServers_.resize(kMaxIceServers);
    iceServerCfg_.clear();
    for (WebRTCIceServer &server : iceServers_) {
        esp_peer_ice_server_cfg_t cfg = {};
        cfg.stun_url = const_cast<char *>(server.url.c_str());
        cfg.user = server.username.length() ? const_cast<char *>(server.username.c_str()) : nullptr;
        cfg.psw = server.credential.length() ? const_cast<char *>(server.credential.c_str()) : nullptr;
        iceServerCfg_.push_back(cfg);
    }

    esp_peer_cfg_t cfg = {};
    cfg.server_lists = iceServerCfg_.empty() ? nullptr : iceServerCfg_.data();
    cfg.server_num = static_cast<uint8_t>(iceServerCfg_.size());
    cfg.role = ESP_PEER_ROLE_CONTROLLED;
    cfg.enable_data_channel = true;
    cfg.manual_ch_create = true;  // the viewer creates the channel
    cfg.no_auto_reconnect = true;
    cfg.ctx = this;
    cfg.on_msg = onMessage;
    cfg.on_state = onState;
    cfg.on_channel_open = onChannelOpen;
    cfg.on_channel_close = onChannelClose;
    cfg.on_data = onData;

    // Viewers offer audio only when getCameraCapabilities reported it; older viewers never do.
    audioActive_ = config_.audio && audioSource_ && strstr(cmd.offer, "m=audio") != nullptr;
    if (audioActive_) {
        cfg.audio_info = {ESP_PEER_AUDIO_CODEC_G711U, 8000, 1};
        cfg.audio_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY;
    }

    dataChannelOffered_ = strstr(cmd.offer, "webrtc-datachannel") != nullptr;
    // Alexa and Google Home accept nothing below 480p, and have no DataChannel to ask for a
    // different size with, so the larger mode is chosen for them up front.
    if (!dataChannelOffered_ && config_.h264Width < 640) {
        config_.h264Width = 640;
        config_.h264Height = 480;
    }
    videoActive_ = config_.h264 && offerWantsH264(cmd.offer) && selectCameraFormat(true);
    if (videoActive_) {
        cfg.video_info = {ESP_PEER_VIDEO_CODEC_H264, config_.h264Width, config_.h264Height, config_.h264Fps};
        cfg.video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY;
    }

    peerDefaults_ = {};
    // Milliseconds the ICE agent waits for a reply. A LAN round trip is a couple of ms, but a
    // smart display answers from a distant region: at 10 ms the DTLS ClientHello timed out long
    // before the reply arrived and the handshake retried forever. Espressif's examples use 500.
    peerDefaults_.agent_recv_timeout = 500;
    peerDefaults_.data_ch_cfg.send_cache_size = config_.dataChannelSendCache;
    peerDefaults_.data_ch_cfg.recv_cache_size = config_.dataChannelRecvCache;
    // RTP carries only the optional PCMU track, so a data-channel-only session would otherwise
    // strand internal RAM that the Wi-Fi driver needs for its dynamic TX buffers. Zero is not an
    // option here: esp_peer reads it as "use the 400 kB default".
    // An encoded frame becomes some 40 RTP packets, which is what the video pool is sized for.
    if (videoActive_) {
        peerDefaults_.rtp_cfg.send_pool_size = 64 * 1024;
        peerDefaults_.rtp_cfg.send_queue_num = 64;
    } else {
        peerDefaults_.rtp_cfg.send_pool_size = audioActive_ ? 48 * 1024 : 4 * 1024;
        peerDefaults_.rtp_cfg.send_queue_num = audioActive_ ? 64 : 8;
    }
    cfg.extra_cfg = &peerDefaults_;
    cfg.extra_size = sizeof(peerDefaults_);

    // Callbacks fire only inside loop(), so the first SDP reported after signal() is our answer.
    int ret = rtc_.begin(cfg);
    if (!ret)
        ret = rtc_.startConnection();
    if (!ret)
        ret = rtc_.signal(ESP_PEER_MSG_TYPE_SDP, reinterpret_cast<const uint8_t *>(cmd.offer), strlen(cmd.offer));

    if (ret) {
        log_e("Peer start failed: %d", ret);
        publishAnswer(false, String("Camera could not start WebRTC (error ") + ret + ")");
        closePeer();
    }
}

void SinricProWebRTCSession::closePeer() {
    streamer_.reset();
    // Before the peer closes: the encoder task holds camera buffers while it runs.
    stopH264();
    controls_.viewerLeft();
    channelOpen_ = false;
    closeRequested_ = false;
    audioActive_ = false;
    capabilitiesPending_ = statePending_ = false;
    rtc_.end();
    if (!answerPublished_)
        publishAnswer(false, "Camera closed the WebRTC session before answering");
}

void SinricProWebRTCSession::publishAnswer(bool ok, const String &error) {
    xSemaphoreTake(answerLock_, portMAX_DELAY);
    answerSequence_ = activeSequence_;
    answerOk_ = ok;
    answer_ = ok ? buildAnswer() : String();
    answerError_ = error;
    xSemaphoreGive(answerLock_);

    answerPublished_ = true;
    xSemaphoreGive(answerReady_);
}

String SinricProWebRTCSession::buildAnswer() const {
    String sdp = localSdp_;
    const char *eol = sdp.indexOf("\r\n") >= 0 ? "\r\n" : "\n";

    String extra;
    for (const String &candidate : localCandidates_) {
        if (sdp.indexOf(candidate) >= 0)
            continue;
        extra += "a=";
        extra += candidate;
        extra += eol;
    }
    if (!extra.length())
        return sdp;

    // Signaling is a single exchange, so separately reported candidates must ride in the SDP.
    // With BUNDLE all media share one transport; the first media section carries them.
    const String eolM = String(eol) + "m=";
    int first = sdp.indexOf("m=");
    int next = first >= 0 ? sdp.indexOf(eolM, first) : -1;
    if (next < 0) {
        if (!sdp.endsWith(eol))
            sdp += eol;
        return sdp + extra;
    }
    size_t split = next + strlen(eol);
    return sdp.substring(0, split) + extra + sdp.substring(split);
}

int SinricProWebRTCSession::onMessage(esp_peer_msg_t *msg, void *ctx) {
    auto *self = static_cast<SinricProWebRTCSession *>(ctx);
    if (!msg || msg->size <= 0 || static_cast<size_t>(msg->size) > kMaxSignalBytes)
        return -1;

    String text;
    text.concat(reinterpret_cast<const char *>(msg->data), msg->size);

    if (msg->type == ESP_PEER_MSG_TYPE_SDP) {
        self->localSdp_ = text;
    } else if (msg->type == ESP_PEER_MSG_TYPE_CANDIDATE) {
        text.trim();
        if (text.startsWith("a="))
            text.remove(0, 2);
        if (text.startsWith("candidate:"))
            self->localCandidates_.push_back(text);
    }

    self->lastSignal_ = millis();
    return 0;
}

int SinricProWebRTCSession::onState(esp_peer_state_t state, void *ctx) {
    auto *self = static_cast<SinricProWebRTCSession *>(ctx);
    log_d("Peer state: %d", state);
    if (state == ESP_PEER_STATE_DISCONNECTED || state == ESP_PEER_STATE_CONNECT_FAILED)
        self->closeRequested_ = true;
    // Encoding starts only once there is somewhere to send frames.
    else if (state == ESP_PEER_STATE_CONNECTED && self->videoActive_ && !self->h264_.running())
        self->startH264();
    else if (state == ESP_PEER_STATE_VIDEO_PLI_RECEIVED)
        self->h264_.requestKeyframe();
    return 0;
}

int SinricProWebRTCSession::onChannelOpen(esp_peer_data_channel_info_t *ch, void *ctx) {
    auto *self = static_cast<SinricProWebRTCSession *>(ctx);
    self->channelId_ = ch->stream_id;
    self->channelOpen_ = true;
    self->capabilitiesPending_ = self->statePending_ = true;
    return 0;
}

int SinricProWebRTCSession::onChannelClose(esp_peer_data_channel_info_t *, void *ctx) {
    auto *self = static_cast<SinricProWebRTCSession *>(ctx);
    self->channelOpen_ = false;
    self->closeRequested_ = true;
    return 0;
}

int SinricProWebRTCSession::onData(esp_peer_data_frame_t *frame, void *ctx) {
    auto *self = static_cast<SinricProWebRTCSession *>(ctx);
    if (!frame || frame->type != ESP_PEER_DATA_CHANNEL_STRING || frame->size <= 0 ||
        static_cast<size_t>(frame->size) > kMaxControlBytes)
        return 0;

    String message;
    message.concat(reinterpret_cast<const char *>(frame->data), frame->size);
    self->controls_.apply(message);
    self->statePending_ = true;
    return 0;
}

void SinricProWebRTCSession::freeCommand(Command &cmd) {
    free(cmd.offer);
    delete cmd.iceServers;
    cmd.offer = nullptr;
    cmd.iceServers = nullptr;
}
