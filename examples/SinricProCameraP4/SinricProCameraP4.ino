// SinricPro camera on an ESP32-P4 with a MIPI-CSI camera: live view in the SinricPro portal and
// app, with H.264 encoded in hardware on a WebRTC video track. Viewers that ask for it get JPEG
// over a WebRTC DataChannel instead. Signaling runs through SinricPro (getWebRTCAnswer), and
// STUN/TURN servers arrive with each offer, so remote viewing works outside your LAN.
//
// Hardware: an ESP32-P4 board with on-board Wi-Fi (an ESP32-C6 wired to the P4 over SDIO and
// running ESP-Hosted, as on the ESP32-P4-Function-EV-Board and Waveshare's ESP32-P4 Wi-Fi boards)
// and an OV5647 camera (Raspberry Pi Camera Rev 1.3 and compatibles) on the CSI connector. Use a
// camera ribbon cable: a Raspberry Pi display cable has a different pinout.
//
// Arduino IDE: board "ESP32P4 Dev Module" (or your board's entry), Tools -> Chip Variant matching
// your chip (the boot log prints its revision: "Before v3.00" for v1.x), PSRAM enabled, and
// Partition Scheme "Huge APP".
//
// Portal setup: device type Camera -> Camera Stream Configuration: Board "ESP32", Streaming
// Protocol "WebRTC", H.264 video track ticked.
//
// Requires: SinricPro library 5.1.0 or later, ESP32 core 3.3.11.

#include <WiFi.h>
#include <SinricPro.h>
#include <SinricProCamera.h>
#include <SinricProWebRTC.h>
#include <SinricProWebRTCSession.h>
#include <WebRTCCamera.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <esp_heap_caps.h>
#include "Settings.h"

#if !CONFIG_IDF_TARGET_ESP32P4
#error "This example is for the ESP32-P4; use SinricProCamera for ESP32 and ESP32-S3 boards."
#endif

// Snapshots go to the portal and to Alexa's SmartVision; quality matters more than size there.
constexpr uint8_t kSnapshotJpegQuality = 85;

SinricProWebRTCSession session;
// Kept so the session can switch the camera to the encoder's input format for an H.264 track.
camera_config_t cameraConfig;

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

bool onSnapshot(const String &deviceId) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Snapshot capture failed");
        return false;
    }

    SinricProCamera &camera = SinricPro[deviceId];
    int status = 0;
    if (fb->format == PIXFORMAT_JPEG) {
        status = camera.sendSnapshot(fb->buf, fb->len);
    } else {
        // An H.264 session leaves the camera in BGR888, so the frame is compressed for upload.
        uint8_t *jpeg = nullptr;
        size_t jpegLen = 0;
        if (frame2jpg(fb, kSnapshotJpegQuality, &jpeg, &jpegLen)) {
            status = camera.sendSnapshot(jpeg, jpegLen);
            free(jpeg);
        }
    }
    esp_camera_fb_return(fb);
    Serial.printf("Snapshot: %s\n", status == 200 ? "sent" : "failed");
    return status == 200;
}

bool onPowerState(const String &deviceId, bool &state) {
    if (!state)
        session.stop();
    return true;
}

void setupCamera() {
    camera_config_t config = {};
    config.pin_pwdn = -1;
    config.pin_reset = -1;
    config.pin_xclk = -1;
    config.pin_sccb_sda = CAMERA_SCCB_SDA;
    config.pin_sccb_scl = CAMERA_SCCB_SCL;
    // The sensor has its own oscillator; the session only requires this to be set.
    config.xclk_freq_hz = 24000000;
    config.pixel_format = PIXFORMAT_JPEG;
    // Every size up to SVGA maps to the sensor's 800x640 mode.
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
    cameraConfig = config;

    esp_err_t err = WebRTCCamera::begin(config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x (%s). Check the ribbon cable, CAMERA_SCCB_SDA/SCL and PSRAM.\n", err,
                      esp_err_to_name(err));
        while (true)
            delay(1000);
    }
    Serial.printf("Camera ready, PSRAM: %u bytes\n", static_cast<unsigned>(ESP.getPsramSize()));
}

void setupWiFi() {
    // Wi-Fi runs on the board's ESP32-C6 through ESP-Hosted; the WiFi API is unchanged.
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    WiFi.setSleep(false);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\nWiFi connected: %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), static_cast<int>(WiFi.RSSI()));
}

void setupWebRTC() {
    SinricProWebRTCSession::Config config;
    config.maxFrameSize = FRAMESIZE_SVGA;  // the size setupCamera() initialized the camera with
    // 800x640 JPEG frames run larger than the default limit for smaller ESP32 sensors.
    config.maxFrameBytes = 192 * 1024;
    config.cameraConfig = cameraConfig;
    config.h264 = true;
    config.h264Width = H264_WIDTH;
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
    camera.enableWebRTCVideo(true);  // without it viewers never offer a video track and get JPEG
    camera.onPowerState(onPowerState);

    SinricPro.onConnected([] { Serial.println("Connected to SinricPro"); });
    SinricPro.onDisconnected([] { Serial.println("Disconnected from SinricPro"); });
    SinricPro.begin(APP_KEY, APP_SECRET);
}

void setup() {
    Serial.begin(115200);
    setupCamera();
    setupWiFi();
    setupWebRTC();
    setupSinricPro();
}

void loop() {
    SinricPro.handle();

    static uint32_t lastHeapLog = 0;
    if (millis() - lastHeapLog > 30000) {
        lastHeapLog = millis();
        Serial.printf("Heap free %u, internal %u (largest %u), PSRAM free %u, RSSI %d, streaming: %s\n",
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                      static_cast<unsigned>(ESP.getFreePsram()), static_cast<int>(WiFi.RSSI()),
                      session.isStreaming() ? "yes" : "no");
    }
}
