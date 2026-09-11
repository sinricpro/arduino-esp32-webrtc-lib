# SinricPro WebRTC

WebRTC for Arduino ESP32 and ESP32-S3, powered by Espressif's `esp_peer` engine. Includes a camera doorbell example with a browser viewer, microphone streaming on XIAO ESP32S3 Sense, and ring/call controls.

The IDF dependencies are bundled as precompiled static libraries (`.a`). No ESP-IDF or WSL setup is needed to use the library in Arduino IDE.

## Requirements

- **Arduino ESP32 3.3.11**, installed through Arduino Boards Manager. **The bundled archives require this exact version**. The exact version is required because we bundled precompiled ESP-IDF dependencies. If you would like to using another version take a look at [BUILDING.md](BUILDING.md)
- An ESP32 or ESP32-S3 camera board with **PSRAM enabled**.
- An application partition of at least **3 MB**.
- Chrome or Edge on the same local network as the board.

## Installation

1. Generate the installable ZIP from this repository with `python tools/package.py`.
2. In Arduino IDE, select **Sketch > Include Library > Add .ZIP Library** and choose `dist/SinricProWebRTC-0.1.0.zip`.
3. Open **File > Examples > SinricPro WebRTC > Doorbell**.

Alternatively, copy this repository into your sketchbook's `libraries/SinricProWebRTC` folder. Python is only needed to generate the ZIP, not to use the library.

## Board setup

Select the matching profile in the example's [Settings.h](examples/Doorbell/Settings.h) and configure Arduino IDE as follows:

| Board | `DOORBELL_BOARD` | Arduino IDE board | PSRAM setting |
| --- | --- | --- | --- |
| ESP-EYE | `BOARD_ESP_EYE` | ESP32 Dev Module | Enabled |
| XIAO ESP32S3 Sense | `BOARD_XIAO_S3_SENSE` | XIAO_ESP32S3 | OPI PSRAM |
| Freenove ESP32-S3-WROOM FNK0085 with camera | `BOARD_FREENOVE_S3` | ESP32S3 Dev Module | OPI PSRAM for N8R8 |
| M5Camera model A | `BOARD_M5CAMERA` | ESP32 Dev Module | Enabled |
| M5Camera model B | `BOARD_M5CAMERA_B` | ESP32 Dev Module | Enabled |
| AI-Thinker ESP32-CAM compatible | `BOARD_AI_THINKER` | ESP32 Dev Module | Enabled |

Match flash size and PSRAM type to your module. On generic ESP32 and ESP32-S3 boards, select **Huge APP (3MB No OTA/1MB SPIFFS)**. XIAO's default 8 MB layout provides a 3 MB application partition.

M5Camera profiles cover models A and B with PSRAM; other M5 camera products and revisions may use different pins. Camera pin mappings are in the sketch's [CameraConfig.h](examples/Doorbell/CameraConfig.h).

All six profiles have passed compilation. Live camera streaming has been verified on an ESP32 with an AI-Thinker-compatible pinout. Other boards and microphone audio still need hardware validation; see [validation results](VALIDATION.md).

## Run the doorbell example

1. Edit the existing definitions in `Settings.h`:

   ```cpp
   #define DOORBELL_BOARD BOARD_XIAO_S3_SENSE

   static const char WIFI_SSID[] = "YOUR_WIFI_SSID";
   static const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
   static const char VIEWER_TOKEN[] = "your-viewer-token";
   ```

2. Select your board and serial port, then upload.
3. Open Serial Monitor at **115200 baud**.
4. Open the printed `http://<board-ip>/` address in Chrome or Edge.
5. Enter your viewer token, click **Connect**, then **Accept call** to start streaming.

Type `r` in Serial Monitor to ring the doorbell. To use a physical button, set `RING_BUTTON_PIN` to an unused GPIO and connect an active-low button between that pin and GND.

**End call** stops streaming. **Disconnect** releases the connection. Only one viewer can connect at a time; abandoned sessions expire after 15 seconds without viewer polling. **Open door** returns `NO_LOCK_CONFIGURED`; the example does not drive a lock.

## Custom cameras

Camera configuration belongs to your sketch. Each example includes its own `CameraConfig.h` with editable board presets; the library has no built-in board list.

Pass a standard `camera_config_t` to `WebRTCCamera::begin()`:

```cpp
CameraSetup::prepare(cameraBoard()); // Example-specific GPIO setup.
camera_config_t config = CameraSetup::config(cameraBoard());
config.frame_size = FRAMESIZE_VGA;
config.jpeg_quality = 16;
esp_err_t result = WebRTCCamera::begin(config);
```

You can replace the preset with your own fully populated `camera_config_t`, including data pins, clock, SCCB, reset/power pins, pixel format, and buffer settings. Perform any board-specific GPIO setup before calling `begin()`. No library changes or archive rebuild are needed. The driver must support the camera and wiring; a custom pin mapping cannot add support for an unsupported sensor.

The Doorbell viewer expects JPEG frames and limits each frame to 128 KiB. Keep `PIXFORMAT_JPEG` and choose resolution and quality accordingly. Its microphone setup is separate and defaults to XIAO Sense only; disable `DOORBELL_MIC` when adapting that profile to a different board.

For migration, replace `WebRTCCamera::begin(board)` with `WebRTCCamera::begin(config)`. The former library board enum and presets now live under `CameraSetup` in each example.

## Capabilities and limits

| Feature | Included behavior |
| --- | --- |
| Camera | 320 x 240 JPEG images, up to 5 fps, over an encrypted WebRTC data channel |
| Browser viewer | Served directly by the board; reassembles and displays JPEG frames |
| Microphone | XIAO Sense onboard PDM microphone, sent as an 8 kHz PCMU/G.711 audio track |
| Controls | Ring, accept, end call, and an open-door command placeholder |
| Signaling | Local HTTP with a viewer token |

JPEG camera streaming requires the included viewer; it is not a native WebRTC video track. H.264 encoding, speaker playback, two-way audio, and acoustic echo cancellation are not included. Microphones on other board profiles are disabled by default.

The example uses direct connections on a trusted LAN. HTTP signaling and the viewer token are unencrypted, although WebRTC media and data transport are encrypted. Its browser candidate adapter assumes a direct LAN connection and does not support reverse proxies or NAT. Remote access requires authenticated HTTPS/WebSocket signaling and suitable ICE/STUN/TURN configuration.

## Library API

```cpp
#include <SinricProWebRTC.h>

SinricWebRTC peer;
```

The header is named `SinricProWebRTC.h`; the wrapper class is `SinricWebRTC`.

Connect Wi-Fi first, then configure an `esp_peer_cfg_t` with your callbacks and ICE settings. The [Doorbell example](examples/Doorbell/Doorbell.ino) provides a complete integration.

| Method | Purpose |
| --- | --- |
| `begin(config)` | Create a peer with the supplied configuration |
| `startConnection()` | Start connection negotiation |
| `loop()` | Process peer work; call frequently |
| `signal(type, data, size)` | Deliver incoming SDP or ICE candidates |
| `sendText(...)`, `sendBinary(...)` | Send data-channel messages |
| `sendAudio(...)`, `sendVideo(...)` | Send encoded media matching the negotiated codec |
| `end()` | Release the peer |
| `handle()` | Access the underlying Espressif peer handle |

Forward outbound SDP and ICE candidates from the `on_msg` callback through your signaling service. `sendAudio()` expects encoded audio, not raw PCM; `encodeMuLaw()` converts one PCM16 sample to G.711 mu-law. `sendVideo()` does not encode camera frames.

Use one peer instance at a time. Serialize peer operations and keep configuration storage and callback context alive until `end()`. Do not create or destroy a peer inside its callbacks. The example runs peer operations in a dedicated FreeRTOS task and uses queues to communicate with the HTTP server.

## Troubleshooting

| Problem | Check |
| --- | --- |
| Core version error | Install ESP32 core **3.3.11**; other versions need matching archives and an updated version guard |
| Sketch exceeds available space | Select a partition with a 3 MB application slot |
| Camera fails to initialize | Board profile, camera ribbon, PSRAM settings, and power supply |
| Viewer cannot connect | Same LAN, correct token, firewall rules, and Wi-Fi client isolation; disconnect a VPN if it prevents LAN ICE connectivity |
| Audio does not play | Use XIAO Sense with `DOORBELL_MIC` enabled; press Play in the browser audio control if autoplay is blocked |

The [HardwareCheck example](examples/HardwareCheck/HardwareCheck.ino) provides camera, crypto, and peer initialization diagnostics. Select its camera profile before uploading.

## Build from source

On Windows, install Python **3.12 or newer**, Git, Arduino ESP32 core **3.3.11**, and Node.js for the viewer tests. Use a project path without spaces and run these commands from the project root, stopping if a command fails:

```powershell
python tools/fetch_sources.py
python tools/build_archives.py --target esp32
python tools/build_archives.py --target esp32s3
python tests/archives.py
node tests/viewer.test.cjs
python tools/compile_matrix.py
python tools/package.py
```

The scripts use the SDK and toolchain under `%LOCALAPPDATA%/Arduino15`; no separate ESP-IDF or WSL installation is required. The compile script finds Arduino CLI on PATH or in the standard Arduino IDE installation. Set `ARDUINO_CLI` to its executable path if installed elsewhere.

The build replaces the archives in `src/esp32/` and `src/esp32s3/` and generates `dist/SinricProWebRTC-0.1.0.zip`. It compiles the available adapter, transport, libSRTP, and private Mbed TLS sources, and includes Espressif's supplied peer-engine binary. It is not a complete source rebuild of that engine.

See [BUILDING.md](BUILDING.md) for the full setup, HardwareCheck compilation, hardware validation, and installation procedure.

## Supporting multiple Arduino core versions

The bundled archives require **3.3.11** because they were compiled against its ESP-IDF SDK and toolchain. Another core can change internal APIs or data layouts; removing the version guard does not establish compatibility.

Archives can be built for additional versions after adapting the SDK paths, dependency revisions, compiler settings, and version checks, then validating compilation and hardware behavior for each version. **The current scripts and CI support 3.3.11 only; there is no automatic core-version selection or `--core-version` option yet.**

Distribute a separate library ZIP for each validated core, such as `SinricProWebRTC-0.1.0-arduino-3.3.11.zip`, and install one variant at a time. Arduino selects precompiled archives by processor, not Arduino core version, so version-named subdirectories alone cannot select the correct archive.

Follow the [porting and distribution steps](BUILDING.md#port-to-another-core-version) before adding a version to the CI build matrix.

## Automated builds

The GitHub Actions workflow in `.github/workflows/build.yml` runs on pull requests, pushes to the repository's default branch (including PR merges), and manual dispatch. It uses Windows with Arduino ESP32 core 3.3.11 to rebuild both archives, check crypto symbol isolation, test the viewer, compile all six Doorbell profiles and HardwareCheck, and package the library.

Download the installable ZIP from the successful run's `SinricProWebRTC-<commit>` artifact in the Actions tab. Compiler logs are uploaded separately. The workflow does not flash hardware or publish a release.

## License and credits

Based on Espressif's esp-webrtc-solution doorbell demo and `esp_peer` engine. See [LICENSE](LICENSE) for this project's license and [THIRD_PARTY.md](THIRD_PARTY.md) for upstream sources, pinned revisions, and dependency licenses.
