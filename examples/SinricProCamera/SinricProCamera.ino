// SinricPro camera with live view in the SinricPro portal and app.
// Video is JPEG over a WebRTC DataChannel; signaling runs through SinricPro (getWebRTCAnswer),
// and STUN/TURN servers arrive with each offer, so remote viewing works outside your LAN.
// Viewers can change resolution / frame rate and toggle flash, flip and mirror; quality drops
// automatically on slow links. XIAO ESP32S3 Sense also streams its onboard microphone.
//
// Portal setup: device type Camera -> Camera Stream Configuration: Board "ESP32",
// Streaming Protocol "WebRTC". Alexa and Google Home streaming is not supported yet.
//
// Requires: SinricPro library 5.1.0 or later, ESP32 core 3.3.10/3.3.11,
// PSRAM enabled and an app partition of at least 3 MB.

#include <WiFi.h>
#include <SinricPro.h>
#include <SinricProCamera.h>
#include <SinricProWebRTC.h>
#include <SinricProWebRTCSession.h>
#include <WebRTCCamera.h>
#include <esp_heap_caps.h>
#include "Settings.h"
#include "CameraConfig.h"

#if CAMERA_BOARD == BOARD_XIAO_S3_SENSE
#define WEBRTC_MIC 1
#include <ESP_I2S.h>
I2SClass microphone;
#else
#define WEBRTC_MIC 0
#endif

// Flash LED viewers can toggle (-1 = none). AI-Thinker ESP32-CAM has its flash on GPIO 4.
#if CAMERA_BOARD == BOARD_AI_THINKER
#define FLASH_LED_PIN 4
#else
#define FLASH_LED_PIN -1
#endif

SinricProWebRTCSession session;

constexpr CameraSetup::Board cameraBoard() {
#if CAMERA_BOARD == BOARD_ESP_EYE
    return CameraSetup::Board::EspEye;
#elif CAMERA_BOARD == BOARD_XIAO_S3_SENSE
    return CameraSetup::Board::XiaoS3Sense;
#elif CAMERA_BOARD == BOARD_FREENOVE_S3
    return CameraSetup::Board::FreenoveS3;
#elif CAMERA_BOARD == BOARD_M5CAMERA
    return CameraSetup::Board::M5Camera;
#elif CAMERA_BOARD == BOARD_M5CAMERA_B
    return CameraSetup::Board::M5CameraB;
#elif CAMERA_BOARD == BOARD_AI_THINKER
    return CameraSetup::Board::AiThinker;
#elif CAMERA_BOARD == BOARD_WROVER_KIT
    return CameraSetup::Board::WroverKit;
#elif CAMERA_BOARD == BOARD_ESP32S3_WROOM
    return CameraSetup::Board::Esp32S3Wroom;
#elif CAMERA_BOARD == BOARD_ESP32S3_GOOUUU
    return CameraSetup::Board::Esp32S3Goouuu;
#elif CAMERA_BOARD == BOARD_LILYGO_CAMERA
    return CameraSetup::Board::LilygoCamera;
#else
#error "Unknown CAMERA_BOARD; choose a profile from BoardProfiles.h."
#endif
}

bool onWebRTCOffer(const String &deviceId, const String &offerSdp, const std::vector<SinricProIceServer> &iceServers,
                   String &answerSdp) {
    std::vector<WebRTCIceServer> servers;
    for (const SinricProIceServer &server : iceServers)
        servers.push_back({server.url, server.username, server.credential});

    Serial.printf("WebRTC offer for %s with %u ICE server URLs\n", deviceId.c_str(), static_cast<unsigned>(servers.size()));
    bool ok = session.handleOffer(offerSdp, servers, answerSdp);
    if (ok) {
        Serial.println("WebRTC answer sent");
    } else {
        // Shown to the viewer in the SinricPro app and portal.
        Serial.printf("WebRTC answer failed: %s\n", session.lastError().c_str());
        SinricPro.setResponseMessage(session.lastError());
    }
    return ok;
}

#if WEBRTC_MIC
// 20 ms of 16 kHz PDM audio, averaged down to the 8 kHz PCMU used by the WebRTC audio track.
bool readMicrophone(uint8_t *pcmu, size_t size) {
    static int16_t pcm[320];
    static size_t used = 0;
    size_t bytes = 0;

    i2s_channel_read(microphone.rxChan(), reinterpret_cast<uint8_t *>(pcm) + used, sizeof(pcm) - used, &bytes, 0);
    used += bytes;
    if (used < sizeof(pcm))
        return false;
    used = 0;

    for (size_t i = 0; i < size && 2 * i + 1 < 320; i++) {
        int16_t sample = (static_cast<int32_t>(pcm[2 * i]) + pcm[2 * i + 1]) / 2;
        pcmu[i] = SinricProWebRTC::encodeMuLaw(sample);
    }
    return true;
}

void setupMicrophone() {
    microphone.setPinsPdmRx(42, 41);
    if (!microphone.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        Serial.println("Microphone initialization failed");
        while (true)
            delay(1000);
    }
}
#endif

bool onSnapshot(const String &deviceId) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
        return false;

    SinricProCamera &camera = SinricPro[deviceId];
    int status = camera.sendSnapshot(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return status == 200;
}

bool onPowerState(const String &deviceId, bool &state) {
    if (!state)
        session.stop();
    return true;
}

void setupCamera() {
    CameraSetup::prepare(cameraBoard());
    camera_config_t config = CameraSetup::config(cameraBoard());
    // Buffers are sized for the init resolution, so init at the largest size viewers may pick.
    config.frame_size = FRAMESIZE_SVGA;
    esp_err_t err = WebRTCCamera::begin(config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x (%s). Check CAMERA_BOARD and PSRAM.\n", err, esp_err_to_name(err));
        while (true)
            delay(1000);
    }
    sensor_t *sensor = esp_camera_sensor_get();
    sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    Serial.printf("Camera: %s, PSRAM: %u bytes\n", CameraSetup::name(cameraBoard()),
                  static_cast<unsigned>(ESP.getPsramSize()));
}

void setupWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    WiFi.setSleep(false);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());
}

void setupWebRTC() {
    SinricProWebRTCSession::Config config;
    config.maxFrameSize = FRAMESIZE_SVGA;  // the size setupCamera() initialized the camera with
    config.flashPin = FLASH_LED_PIN;
#if CONFIG_IDF_TARGET_ESP32
    // Once Wi-Fi and the SinricPro TLS socket are up, classic ESP32 has a single ~28 kB contiguous
    // internal block left. Caches large enough to consume it leave the Wi-Fi driver unable to
    // allocate its dynamic TX buffers, and sendto() then fails with ENOMEM even for 88-byte STUN
    // packets - the DTLS handshake never completes.
    config.dataChannelSendCache = 6 * 1024;
    config.dataChannelRecvCache = 3 * 1024;
#endif
#if WEBRTC_MIC
    config.audio = true;
    session.setAudioSource(readMicrophone);
#endif
    if (!session.begin(config)) {
        Serial.println("Not enough memory for the WebRTC session task");
        while (true)
            delay(1000);
    }
}

void setupSinricPro() {
    SinricProCamera &camera = SinricPro[CAMERA_ID];
    camera.onSnapshot(onSnapshot);
    camera.onWebRTCOffer(onWebRTCOffer);
    camera.enableWebRTCAudio(WEBRTC_MIC);  // viewers request an audio track only when this is set
    camera.onPowerState(onPowerState);

    SinricPro.onConnected([] { Serial.println("Connected to SinricPro"); });
    SinricPro.onDisconnected([] { Serial.println("Disconnected from SinricPro"); });
    SinricPro.begin(APP_KEY, APP_SECRET);
}

void setup() {
    Serial.begin(115200);
    setupCamera();
#if WEBRTC_MIC
    setupMicrophone();
#endif
    setupWiFi();
    setupWebRTC();
    setupSinricPro();
}

void loop() {
    SinricPro.handle();

    static uint32_t lastHeapLog = 0;
    if (millis() - lastHeapLog > 30000) {
        lastHeapLog = millis();
        // Internal DRAM is the pool that runs out first: Wi-Fi, TLS, DTLS and lwIP pbufs all draw on
        // it and PSRAM cannot substitute. A healthy total beside a small largest block is
        // fragmentation rather than exhaustion, and the two need different fixes.
        Serial.printf("Heap free %u (min %u), internal %u (largest %u), PSRAM free %u, RSSI %d, streaming: %s\n",
                      static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                      static_cast<unsigned>(ESP.getFreePsram()), static_cast<int>(WiFi.RSSI()),
                      session.isStreaming() ? "yes" : "no");
    }
}
