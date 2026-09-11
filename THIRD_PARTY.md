# Third-party components

The archive contains the following components. Preserve LICENSES when distributing it.

| Component | Source revision | License |
| --- | --- | --- |
| Espressif esp_peer 1.5.5, including libpeer_default.a | esp-webrtc-solution c8650846b512e6e1375e5f78c1c41619b8d645eb | Espressif Modified MIT; use with Espressif products only; LICENSES/esp-peer.txt |
| Espressif esp_libsrtp port | esp-adf-libs da256e5f462a8e010667d35314a5ba5cdc4a8d9a | LICENSES/esp-libsrtp.txt |
| libSRTP source bundled by that port | same esp-adf-libs revision | BSD; LICENSES/libsrtp.txt |
| Espressif Mbed TLS fork | 9d669eadb1955d348986b9280156710aaaadf79f, selected by IDF 5.5.5 b774170ff46c393eeb5e495ea37936038d3f4f4f | Apache-2.0 OR GPL-2.0-or-later; distributed here under Apache-2.0; LICENSES/mbedtls.txt |

Sources: https://github.com/espressif/esp-webrtc-solution,
https://github.com/espressif/esp-adf-libs,
https://github.com/espressif/esp-idf,
https://github.com/espressif/mbedtls.

`tools/fetch_sources.py` downloads the pinned sources. `tools/build_archives.py`
compiles the peer adapter, libSRTP and a private software-crypto Mbed TLS configuration
against Arduino's installed IDF headers. It adds an ESP hardware random source and
monotonic clock implementation and renames private crypto symbols with objcopy.
The original Espressif peer binary is included with its crypto references renamed.
No original source files are overwritten by the build.

Camera pin maps were checked against Arduino ESP32 3.3.11 CameraWebServer,
Freenove's ESP32-S3-WROOM CameraWebServer example, and M5Stack-Camera's model A/B
configuration. The XIAO PDM pins follow Seeed's microphone documentation.

## Additional build profiles

The 3.3.10 variant uses ESP-IDF 5.5.4 commit `735507283d5b2f9fb363a1901172dbd9e847945d` and its Mbed TLS submodule `ffb280bb63c78bfec1e1ab55040671768c85c923`, under the same Mbed TLS license terms. Peer and ADF revisions remain as listed above. Per-target `build-info.json` files identify the dependencies in each generated variant.

Additional camera mappings follow Espressif's camera example and LilyGO's original camera-bme280 sketch:

- https://github.com/espressif/esp32-camera/blob/master/examples/camera_example/main/camera_pinout.h
- https://github.com/LilyGO/esp32-camera-bme280/blob/master/esp32-camera-bme280.ino

LILYGO support covers the camera pins only. Sensor, display, and motion-detection libraries are not included.
