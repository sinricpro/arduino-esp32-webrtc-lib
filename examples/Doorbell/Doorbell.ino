#include <WiFi.h>
#include <WebServer.h>
#include <SinricProWebRTC.h>
#include <WebRTCCamera.h>
#include <atomic>
#include <freertos/queue.h>
#include "Settings.h"
#include "CameraConfig.h"
#include "Viewer.h"
#if DOORBELL_MIC
#include <ESP_I2S.h>
I2SClass microphone;
#endif

SET_LOOP_TASK_STACK_SIZE(24 * 1024);
WebServer server(80);
SinricWebRTC rtc;
bool channelReady = false, streaming = false, closeRequested = false;
uint16_t channelId = 0;
std::atomic<uint32_t> lastActivity{0};
std::atomic<bool> sessionActive{false};
enum class Action { Start, Candidate, Stop, Ring };
struct Command { Action action; char *data; size_t size; };
QueueHandle_t commands = nullptr, outgoing = nullptr;
camera_fb_t *frame = nullptr;
size_t frameOffset = 0;
uint32_t frameId = 0, lastFrame = 0, frameStarted = 0;

constexpr CameraSetup::Board cameraBoard() {
#if DOORBELL_BOARD == BOARD_ESP_EYE
    return CameraSetup::Board::EspEye;
#elif DOORBELL_BOARD == BOARD_XIAO_S3_SENSE
    return CameraSetup::Board::XiaoS3Sense;
#elif DOORBELL_BOARD == BOARD_FREENOVE_S3
    return CameraSetup::Board::FreenoveS3;
#elif DOORBELL_BOARD == BOARD_M5CAMERA
    return CameraSetup::Board::M5Camera;
#elif DOORBELL_BOARD == BOARD_M5CAMERA_B
    return CameraSetup::Board::M5CameraB;
#elif DOORBELL_BOARD == BOARD_AI_THINKER
    return CameraSetup::Board::AiThinker;
#else
#error "Unknown DOORBELL_BOARD"
#endif
}

void releaseFrame() {
    if (frame) esp_camera_fb_return(frame);
    frame = nullptr; frameOffset = 0;
}
void stopSession() {
    streaming = channelReady = false;
    releaseFrame(); rtc.end();
    char *msg;
    while (xQueueReceive(outgoing, &msg, 0) == pdTRUE) free(msg);
    closeRequested = false; sessionActive = false;
}
int onSignal(esp_peer_msg_t *msg, void *) {
    if (WEBRTC_DEBUG) Serial.printf("SIGNAL %d (%d): %.*s\n", msg->type, msg->size, msg->size, msg->data);
    if (msg->size <= 0 || msg->size > 16384) {
        closeRequested = true; return -1;
    }
    char *value = static_cast<char*>(malloc(msg->size + 2));
    if (!value) {
        closeRequested = true; return -1;
    }
    value[0] = msg->type == ESP_PEER_MSG_TYPE_SDP ? 'S' : 'C';
    memcpy(value + 1, msg->data, msg->size); value[msg->size + 1] = 0;
    if (xQueueSend(outgoing, &value, 0) != pdTRUE) {
        free(value); closeRequested = true; return -1;
    }
    return 0;
}
int onState(esp_peer_state_t state, void *) {
    Serial.printf("Peer state: %d\n", state);
    if (state == ESP_PEER_STATE_DISCONNECTED || state == ESP_PEER_STATE_CONNECT_FAILED)
        closeRequested = true;
    return 0;
}
int onChannel(esp_peer_data_channel_info_t *ch, void *) {
    channelId = ch->stream_id; channelReady = true;
    Serial.println("Viewer ready. Type r to ring, or click Accept in the browser.");
    return 0;
}
int onChannelClosed(esp_peer_data_channel_info_t *, void *) {
    channelReady = streaming = false;
    return 0;
}
int onData(esp_peer_data_frame_t *f, void *) {
    if (f->type != ESP_PEER_DATA_CHANNEL_STRING || f->size < 1 || f->size > 64) return 0;
    String command;
    command.concat(reinterpret_cast<const char*>(f->data), f->size);
    if (command == "ACCEPT_CALL") streaming = true;
    else if (command == "DENY_CALL") streaming = false;
    else if (command == "OPEN_DOOR") {
        // Connect your own actuator here, then acknowledge only on success.
        // The default example has no lock attached.
        rtc.sendText(channelId, "NO_LOCK_CONFIGURED");
    }
    return 0;
}
bool authorized() {
    if (server.header("X-Viewer-Token") == VIEWER_TOKEN) return true;
    server.send(403, "text/plain", "Invalid viewer token"); return false;
}
bool enqueue(Action action, const String &data = String()) {
    Command cmd = {action, nullptr, data.length()};
    if (data.length()) {
        cmd.data = static_cast<char*>(malloc(data.length() + 1));
        if (!cmd.data) return false;
        memcpy(cmd.data, data.c_str(), data.length() + 1);
    }
    if (xQueueSend(commands, &cmd, 0) == pdTRUE) return true;
    free(cmd.data); return false;
}
void configureServer() {
    const char *headers[] = {"X-Viewer-Token"};
    server.collectHeaders(headers, 1);
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", VIEWER_HTML); });
    server.on("/config", HTTP_GET, [] {
        if (!authorized()) return;
        server.send(200, "text/plain", String(DOORBELL_MIC ? "audio\n" : "camera\n") + server.client().remoteIP().toString());
    });
    server.on("/offer", HTTP_POST, [] {
        if (!authorized()) return;
        String sdp = server.arg("plain");
        if (sdp.length() > 16384 || !sdp.startsWith("v=0")) {
            server.send(400, "text/plain", "Invalid SDP"); return;
        }
        if (sessionActive.exchange(true)) { server.send(409, "text/plain", "Viewer already connected; disconnect it first"); return; }
        lastActivity = millis();
        if (!enqueue(Action::Start, sdp)) {
            sessionActive = false; server.send(503, "text/plain", "Peer queue full"); return;
        }
        server.send(200, "text/plain", "OK");
    });
    server.on("/signal", HTTP_GET, [] {
        if (!authorized()) return;
        lastActivity = millis();
        server.sendHeader("Cache-Control", "no-store");
        char *msg;
        if (xQueueReceive(outgoing, &msg, 0) == pdTRUE) {
            server.send(200, "text/plain", msg); free(msg);
        } else if (!sessionActive) server.send(410, "text/plain", "Peer session closed");
        else server.send(204);
    });
    server.on("/candidate", HTTP_POST, [] {
        if (!authorized()) return;
        String value = server.arg("plain");
        if (value.length() > 2048 || !value.startsWith("candidate:")) {
            server.send(400, "text/plain", "Invalid candidate"); return;
        }
        bool ok = sessionActive && enqueue(Action::Candidate, value);
        server.send(ok ? 200 : 503, "text/plain", ok ? "OK" : "Peer unavailable");
    });
    server.on("/close", HTTP_POST, [] {
        if (!authorized()) return;
        bool ok = enqueue(Action::Stop);
        server.send(ok ? 200 : 503, "text/plain", ok ? "OK" : "Peer queue full");
    });
    server.begin();
}
void cameraLoop() {
    if (!streaming || !channelReady) { releaseFrame(); return; }
    if (!frame) {
        if (millis() - lastFrame < 200) return; // At most 5 fps initially.
        lastFrame = millis();
        frame = esp_camera_fb_get();
        if (!frame) return;
        if (frame->format != PIXFORMAT_JPEG || frame->len > 128 * 1024) { releaseFrame(); return; }
        frameOffset = 0; ++frameId; frameStarted = millis();
    }
    // Send one bounded fragment per loop, allowing ICE/SCTP and HTTP to run.
    // Header: magic, frame id, total length, offset (four uint32 little endian).
    uint8_t packet[1040];
    uint32_t header[] = {0x47504A53, frameId, static_cast<uint32_t>(frame->len), static_cast<uint32_t>(frameOffset)};
    memcpy(packet, header, sizeof(header));
    size_t bytes = min(size_t(1024), frame->len - frameOffset);
    memcpy(packet + 16, frame->buf + frameOffset, bytes);
    int ret = rtc.sendBinary(channelId, packet, bytes + 16);
    if (ret == 0) frameOffset += bytes;
    if (frameOffset == frame->len || millis() - frameStarted > 1000 || (ret && ret != ESP_PEER_ERR_WOULD_BLOCK))
        releaseFrame();
}
void audioLoop() {
#if DOORBELL_MIC
    static int16_t pcm[320]; // Capture PDM at 16 kHz, then downsample to PCMU's 8 kHz.
    static size_t used = 0;
    static uint32_t pts = 0;
    size_t bytes = 0;
    // Nonblocking DMA read. Drain even while idle to avoid stale audio.
    i2s_channel_read(microphone.rxChan(), reinterpret_cast<uint8_t*>(pcm) + used, sizeof(pcm) - used, &bytes, 0);
    used += bytes;
    if (used != sizeof(pcm)) return;
    used = 0;
    if (streaming && channelReady) {
        uint8_t encoded[160];
        for (int i=0; i<160; ++i) {
            int16_t sample = (static_cast<int32_t>(pcm[2*i]) + pcm[2*i+1]) / 2;
            encoded[i] = SinricWebRTC::encodeMuLaw(sample);
        }
        rtc.sendAudio(encoded, sizeof(encoded), pts);
    }
    pts += 20;
#endif
}
// One task owns every peer operation and media callback. HTTP stays responsive
// while ICE/DTLS does blocking network work. Queues transfer ownership of data.
void peerTask(void *) {
    while (true) {
        Command cmd;
        while (xQueueReceive(commands, &cmd, 0) == pdTRUE) {
            if (cmd.action == Action::Stop) stopSession();
            else if (cmd.action == Action::Ring && channelReady) rtc.sendText(channelId, "RING");
            else if (cmd.action == Action::Candidate && rtc.handle()) {
                rtc.signal(ESP_PEER_MSG_TYPE_CANDIDATE, reinterpret_cast<uint8_t*>(cmd.data), cmd.size);
            } else if (cmd.action == Action::Start) {
                esp_peer_cfg_t cfg = {};
                cfg.role = ESP_PEER_ROLE_CONTROLLED;
                cfg.enable_data_channel = true; cfg.manual_ch_create = true;
                cfg.no_auto_reconnect = true;
                cfg.on_msg = onSignal; cfg.on_state = onState;
                cfg.on_channel_open = onChannel; cfg.on_channel_close = onChannelClosed;
                cfg.on_data = onData;
#if DOORBELL_MIC
                cfg.audio_info = {ESP_PEER_AUDIO_CODEC_G711U, 8000, 1};
                cfg.audio_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY;
#endif
                int ret = rtc.begin(cfg);
                if (!ret) ret = rtc.startConnection();
                if (!ret) ret = rtc.signal(ESP_PEER_MSG_TYPE_SDP, reinterpret_cast<uint8_t*>(cmd.data), cmd.size);
                if (ret) { Serial.printf("Peer start failed: %d\n", ret); closeRequested = true; }
            }
            free(cmd.data);
        }
        if (rtc.handle()) rtc.loop();
        if (closeRequested || (rtc.handle() && millis() - lastActivity.load() > 15000)) stopSession();
        audioLoop(); cameraLoop();
        vTaskDelay(1);
    }
}
void setup() {
    Serial.begin(115200);
    delay(1000);
    if (WEBRTC_DEBUG) esp_log_level_set("*", ESP_LOG_VERBOSE);
    if (RING_BUTTON_PIN >= 0) pinMode(RING_BUTTON_PIN, INPUT_PULLUP);
    CameraSetup::prepare(cameraBoard());
    camera_config_t cameraConfig = CameraSetup::config(cameraBoard());
    // Customize cameraConfig here, or define your own configuration in CameraConfig.h.
    esp_err_t cameraResult = WebRTCCamera::begin(cameraConfig);
    if (cameraResult != ESP_OK) {
        Serial.printf("Camera failed: 0x%x. Check board profile and enable PSRAM.\n", cameraResult);
        while (true) delay(1000);
    }
#if DOORBELL_MIC
#if DOORBELL_BOARD != BOARD_XIAO_S3_SENSE
#error "This example's onboard microphone profile is XIAO ESP32S3 Sense only."
#endif
    microphone.setPinsPdmRx(42, 41);
    if (!microphone.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        Serial.println("Microphone initialization failed"); while (true) delay(1000);
    }
#endif
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setSleep(false);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
    Serial.printf("\nOpen http://%s/ in Chrome or Edge on the same LAN.\n", WiFi.localIP().toString().c_str());
    commands = xQueueCreate(8, sizeof(Command));
    outgoing = xQueueCreate(24, sizeof(char*));
    if (!commands || !outgoing || xTaskCreate(peerTask, "webrtc", 24 * 1024, nullptr, 4, nullptr) != pdPASS) {
        Serial.println("Not enough memory for WebRTC task/queues"); while (true) delay(1000);
    }
    configureServer();
}
void loop() {
    server.handleClient();
    bool ring = Serial.available() && Serial.read() == 'r';
    static bool previous = false;
    static uint32_t lastRing = 0;
    bool pressed = RING_BUTTON_PIN >= 0 && digitalRead(RING_BUTTON_PIN) == LOW;
    ring |= pressed && !previous; previous = pressed;
    if (ring && millis() - lastRing > 300) {
        enqueue(Action::Ring); lastRing = millis();
    }
    delay(1);
}
