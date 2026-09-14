#include "SinricProWebRTC.h"
#include <limits.h>

int SinricProWebRTC::begin(esp_peer_cfg_t cfg) {
    if (peer_) return ESP_PEER_ERR_WRONG_STATE;
    defaults_ = {};
    defaults_.agent_recv_timeout = 10;
    defaults_.data_ch_cfg.send_cache_size = 48 * 1024;
    defaults_.data_ch_cfg.recv_cache_size = 16 * 1024;
    defaults_.rtp_cfg.send_pool_size = 48 * 1024;
    defaults_.rtp_cfg.send_queue_num = 64;
    if (!cfg.extra_cfg) {
        cfg.extra_cfg = &defaults_;
        cfg.extra_size = sizeof(defaults_);
    }
    return esp_peer_open(&cfg, esp_peer_get_default_impl(), &peer_);
}
void SinricProWebRTC::end() {
    if (peer_) { esp_peer_close(peer_); peer_ = nullptr; }
}
int SinricProWebRTC::loop() { return peer_ ? esp_peer_main_loop(peer_) : ESP_PEER_ERR_WRONG_STATE; }
int SinricProWebRTC::startConnection() { return peer_ ? esp_peer_new_connection(peer_) : ESP_PEER_ERR_WRONG_STATE; }
int SinricProWebRTC::signal(esp_peer_msg_type_t type, const uint8_t *data, size_t size) {
    if (!peer_) return ESP_PEER_ERR_WRONG_STATE;
    if (!data || !size || size > INT_MAX || (type != ESP_PEER_MSG_TYPE_SDP && type != ESP_PEER_MSG_TYPE_CANDIDATE))
        return ESP_PEER_ERR_INVALID_ARG;
    esp_peer_msg_t msg = {type, const_cast<uint8_t*>(data), static_cast<int>(size)};
    return esp_peer_send_msg(peer_, &msg);
}
int SinricProWebRTC::sendBinary(uint16_t channel, const uint8_t *data, size_t size) {
    if (!peer_) return ESP_PEER_ERR_WRONG_STATE;
    if (!data || !size || size > INT_MAX) return ESP_PEER_ERR_INVALID_ARG;
    esp_peer_data_frame_t f = {ESP_PEER_DATA_CHANNEL_DATA, channel, const_cast<uint8_t*>(data), static_cast<int>(size)};
    return esp_peer_send_data(peer_, &f);
}
int SinricProWebRTC::sendText(uint16_t channel, const char *text) {
    if (!peer_) return ESP_PEER_ERR_WRONG_STATE;
    if (!text || strlen(text) > INT_MAX) return ESP_PEER_ERR_INVALID_ARG;
    esp_peer_data_frame_t f = {ESP_PEER_DATA_CHANNEL_STRING, channel, reinterpret_cast<uint8_t*>(const_cast<char*>(text)), static_cast<int>(strlen(text))};
    return esp_peer_send_data(peer_, &f);
}
int SinricProWebRTC::sendAudio(const uint8_t *data, size_t size, uint32_t pts) {
    if (!peer_) return ESP_PEER_ERR_WRONG_STATE;
    if (!data || !size || size > INT_MAX) return ESP_PEER_ERR_INVALID_ARG;
    esp_peer_audio_frame_t f = {pts, const_cast<uint8_t*>(data), static_cast<int>(size)};
    return esp_peer_send_audio(peer_, &f);
}
int SinricProWebRTC::sendVideo(const uint8_t *data, size_t size, uint32_t pts) {
    if (!peer_) return ESP_PEER_ERR_WRONG_STATE;
    if (!data || !size || size > INT_MAX) return ESP_PEER_ERR_INVALID_ARG;
    esp_peer_video_frame_t f = {pts, const_cast<uint8_t*>(data), static_cast<int>(size)};
    return esp_peer_send_video(peer_, &f);
}
uint8_t SinricProWebRTC::encodeMuLaw(int16_t pcm) {
    int value = pcm;
    int sign = value < 0 ? 0x80 : 0;
    if (value < 0) value = -value;
    if (value > 32635) value = 32635;
    value += 0x84;
    int exponent = 7;
    for (int mask = 0x4000; exponent > 0 && !(value & mask); mask >>= 1) --exponent;
    return static_cast<uint8_t>(~(sign | (exponent << 4) | ((value >> (exponent + 3)) & 15)));
}
