# Third-party components

The archive contains the following components. Preserve LICENSES when distributing it.

| Component | Source revision | License |
| --- | --- | --- |
| Espressif esp_peer 1.5.5, including libpeer_default.a | esp-webrtc-solution c8650846b512e6e1375e5f78c1c41619b8d645eb | Espressif Modified MIT; use with Espressif products only; LICENSES/esp-peer.txt |
| Espressif esp_libsrtp port | esp-adf-libs da256e5f462a8e010667d35314a5ba5cdc4a8d9a | LICENSES/esp-libsrtp.txt |
| libSRTP source bundled by that port | same esp-adf-libs revision | BSD; LICENSES/libsrtp.txt |
| Espressif Mbed TLS fork | 9d669eadb1955d348986b9280156710aaaadf79f, selected by IDF 5.5.5 b774170ff46c393eeb5e495ea37936038d3f4f4f | Apache-2.0 OR GPL-2.0-or-later; distributed here under Apache-2.0; LICENSES/mbedtls.txt |
| Espressif esp_h264 1.4.1 software encoder (ESP32-S3 archive only), including libopenh264.a | Component registry package `espressif__esp_h264-v1.4.1.zip`, sha256 `c42a7365…05ad4e67` | Apache-2.0; LICENSES/esp-h264.txt |
| Cisco OpenH264 v2.2.0, which Espressif's software encoder is built from | same package, shipped only as that prebuilt library | Redistributed under the esp_h264 component licence above; upstream https://github.com/cisco/openh264 |

Sources: https://github.com/espressif/esp-webrtc-solution,
https://github.com/espressif/esp-adf-libs,
https://github.com/espressif/esp-idf,
https://github.com/espressif/mbedtls,
https://github.com/cisco/openh264.

`tools/fetch_sources.py` downloads the pinned sources, by commit for the Git repositories and by
version plus archive hash for the esp_h264 registry package (esp-adf-libs still ships 0.1.1 from
2023, which predates the software encoder used here). `tools/build_archives.py`
compiles the peer adapter, libSRTP and a private software-crypto Mbed TLS configuration
against Arduino's installed IDF headers. It adds an ESP hardware random source and
monotonic clock implementation and renames private crypto symbols with objcopy.
The original Espressif peer binary is included with its crypto references renamed.
No original source files are overwritten by the build.

For the ESP32-S3 archive it also compiles the esp_h264 software encoder and adds Espressif's
prebuilt `libopenh264.a`; the decoder is left out, since a camera only sends. The encoder's public
headers (`esp_h264_*.h`) are vendored into `src/` alongside the esp_peer headers, because sketches
compile against the prebuilt archive and would otherwise have no declarations.

Camera pin maps were checked against Arduino ESP32 3.3.11 CameraWebServer,
Freenove's ESP32-S3-WROOM CameraWebServer example, and M5Stack-Camera's model A/B
configuration. The XIAO PDM pins follow Seeed's microphone documentation.

## Additional build profiles

The 3.3.10 variant uses ESP-IDF 5.5.4 commit `735507283d5b2f9fb363a1901172dbd9e847945d` and its Mbed TLS submodule `ffb280bb63c78bfec1e1ab55040671768c85c923`, under the same Mbed TLS license terms. Peer and ADF revisions remain as listed above. Per-target `build-info.json` files identify the dependencies in each generated variant.

Additional camera mappings follow Espressif's camera example and LilyGO's original camera-bme280 sketch:

- https://github.com/espressif/esp32-camera/blob/master/examples/camera_example/main/camera_pinout.h
- https://github.com/LilyGO/esp32-camera-bme280/blob/master/esp32-camera-bme280.ino

LILYGO support covers the camera pins only. Sensor, display, and motion-detection libraries are not included.
