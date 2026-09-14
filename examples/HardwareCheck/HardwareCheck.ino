#include <WiFi.h>
#include <SinricProWebRTC.h>
#include <WebRTCCamera.h>
#include "CameraConfig.h"
#include <mbedtls/sha256.h>

// Change for your board. No Wi-Fi credentials or Internet connection required.
static constexpr auto BOARD = CameraSetup::Board::LilygoCamera;
SET_LOOP_TASK_STACK_SIZE(24 * 1024);
SinricProWebRTC rtc;
bool gotSdp = false;
int messages(esp_peer_msg_t *msg, void *) {
    if (msg->type == ESP_PEER_MSG_TYPE_SDP) {
        gotSdp = true;
        Serial.printf("CHECK SDP PASS (%d bytes)\n", msg->size);
    }

    return 0;
}

// The private crypto symbols are intentionally separate from Arduino's Mbed TLS.
extern "C" {
int sinric_private_mbedtls_aes_self_test(int);
int sinric_private_mbedtls_sha256_self_test(int);
int sinric_private_mbedtls_sha1_self_test(int);
int sinric_private_mbedtls_ctr_drbg_self_test(int);
int sinric_private_srtp_init(void);
int sinric_private_srtp_shutdown(void);
}

void setup() {
    Serial.begin(115200);
    delay(1200);
    Serial.printf("CHECK chip %s, PSRAM %u, heap %u\n", ESP.getChipModel(), ESP.getPsramSize(),
                  ESP.getFreeHeap());
    WiFi.mode(WIFI_AP);
    WiFi.softAP("WebRTC-HardwareCheck", "webrtc-check-123");
    Serial.printf("CHECK AES %d\n", sinric_private_mbedtls_aes_self_test(0));
    Serial.printf("CHECK SHA256 %d\n", sinric_private_mbedtls_sha256_self_test(0));
    const uint8_t expected[] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    uint8_t digest[32];
    int coreRet = mbedtls_sha256(reinterpret_cast<const uint8_t *>("abc"), 3, digest, 0);
    Serial.printf("CHECK ARDUINO SHA256 %s\n",
                  !coreRet && !memcmp(digest, expected, 32) ? "PASS" : "FAIL");
    Serial.printf("CHECK SHA1 %d\n", sinric_private_mbedtls_sha1_self_test(0));
    Serial.printf("CHECK DRBG %d\n", sinric_private_mbedtls_ctr_drbg_self_test(0));
    Serial.printf("CHECK SRTP %d\n", sinric_private_srtp_init());
    sinric_private_srtp_shutdown();
    CameraSetup::prepare(BOARD);
    camera_config_t cameraConfig = CameraSetup::config(BOARD);
    int err = WebRTCCamera::begin(cameraConfig);
    Serial.printf("CHECK CAMERA init 0x%x\n", err);
    if (!err) {
        for (int i = 0; i < 10; ++i) {
            camera_fb_t *f = esp_camera_fb_get();
            Serial.printf("CHECK JPEG %d %s %u bytes\n", i,
                          f && f->len > 2 && f->buf[0] == 0xff && f->buf[1] == 0xd8 ? "PASS"
                                                                                    : "FAIL",
                          f ? f->len : 0);
            if (f)
                esp_camera_fb_return(f);
            delay(20);
        }
    }

    uint32_t start = millis();
    int ret = esp_peer_pre_generate_cert();
    Serial.printf("CHECK CERT %d (%u ms)\n", ret, millis() - start);
    esp_peer_cfg_t cfg = {};
    cfg.role = ESP_PEER_ROLE_CONTROLLING;
    cfg.enable_data_channel = true;
    cfg.on_msg = messages;
    ret = rtc.begin(cfg);
    Serial.printf("CHECK PEER open %d\n", ret);
    if (!ret)
        Serial.printf("CHECK PEER start %d\n", rtc.startConnection());
    start = millis();
    while (rtc.handle() && millis() - start < 3000) {
        rtc.loop();
        delay(1);
    }

    rtc.end();
    Serial.printf("CHECK FINISHED SDP=%d heap=%u PSRAM-free=%u\n", gotSdp, ESP.getFreeHeap(),
                  ESP.getFreePsram());
}

void loop() {
    delay(1000);
}
