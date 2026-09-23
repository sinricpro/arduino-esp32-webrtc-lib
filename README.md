# WebRTC for Arduino ESP32

WebRTC for Arduino ESP32 and ESP32-S3, powered by Espressif's `esp_peer` engine. Includes a camera doorbell example with a browser viewer, microphone streaming on XIAO ESP32S3 Sense, and ring/call controls. ESP32-S3 can also send H.264 on a native WebRTC video track.

The IDF dependencies are bundled as precompiled static libraries (`.a`). No ESP-IDF or WSL setup is needed to use the library in Arduino IDE.

## Requirements

- **Arduino ESP32 3.3.11 or 3.3.10**, with the matching library ZIP. Precompiled dependencies require an exact core match; see [BUILDING.md](BUILDING.md).
- An ESP32 or ESP32-S3 camera board with **PSRAM enabled**.
- An application partition of at least **3 MB**.
- A Wi-Fi signal of **−75 dBm or better** at the board. Below about −80 dBm the DTLS handshake cannot complete, because the Wi-Fi driver's transmit buffers stop recycling fast enough; free heap looks healthy throughout. See [VALIDATION.md](VALIDATION.md).
- Chrome or Edge on the same local network as the board.

## Installation

### Library Manager — Arduino ESP32 3.3.11

In Arduino IDE, open **Sketch > Include Library > Manage Libraries**, search for **SinricProWebRTC**, and install it. Library Manager serves the repository tree, which carries the 3.3.11 archives. On any other core the version guard stops the build instead of linking mismatched binaries; install the ZIP for that core instead.

### ZIP — any supported core

1. Download `SinricProWebRTC-<version>-arduino-<core>.zip` for your Arduino core from the [latest release](https://github.com/sinricpro/arduino-esp32-webrtc-lib/releases/latest).
2. In Arduino IDE, select **Sketch > Include Library > Add .ZIP Library** and choose that ZIP. Install one variant at a time.

Either way, open **File > Examples > SinricProWebRTC > Doorbell** once the library is installed.

## Board setup

Select the matching profile in the example's [Settings.h](examples/Doorbell/Settings.h) and configure Arduino IDE as follows:

| Board | `DOORBELL_BOARD` | Arduino IDE board | PSRAM setting |
| --- | --- | --- | --- |
| ESP-EYE | `BOARD_ESP_EYE` | ESP32 Dev Module | Enabled |
| XIAO ESP32S3 Sense | `BOARD_XIAO_S3_SENSE` | XIAO_ESP32S3 | OPI PSRAM |
| Freenove ESP32-S3-WROOM FNK0085 with camera | `BOARD_FREENOVE_S3` | ESP32S3 Dev Module | OPI PSRAM for N8R8 |
| M5Camera model A | `BOARD_M5CAMERA` | ESP32 Dev Module | Enabled |
| M5Camera model B | `BOARD_M5CAMERA_B` | ESP32 Dev Module | Enabled |
| AI-Thinker ESP32-CAM compatible (default) | `BOARD_AI_THINKER` | ESP32 Dev Module | Enabled |
| ESP-WROVER-KIT | `BOARD_WROVER_KIT` | ESP32 Dev Module | Enabled |
| ESP32-S3 WROOM camera wiring with PWDN GPIO38 | `BOARD_ESP32S3_WROOM` | ESP32S3 Dev Module | Match module |
| GOOUUU ESP32-S3 camera wiring | `BOARD_ESP32S3_GOOUUU` | ESP32S3 Dev Module | Match module |
| LILYGO TTGO T-Camera / camera-bme280 (camera only) | `BOARD_LILYGO_CAMERA` | ESP32 Dev Module | Enabled |

Match flash size and PSRAM type to your module. On generic ESP32 and ESP32-S3 boards, select **Huge APP (3MB No OTA/1MB SPIFFS)**. XIAO's default 8 MB layout provides a 3 MB application partition.

M5Camera profiles cover models A and B with PSRAM; other M5 camera products and revisions may use different pins. Camera pin mappings are in the sketch's [CameraConfig.h](examples/Doorbell/CameraConfig.h).

The compile matrix covers all ten profiles on both core versions. Camera streaming has been tested on an AI-Thinker-compatible ESP32 and the LILYGO TTGO T-Camera (ESP32-WROVER-B / OV2640). Other physical boards and microphone audio still need validation. See [validation results](VALIDATION.md).

The added profiles follow [Espressif camera_pinout.h](https://github.com/espressif/esp32-camera/blob/master/examples/camera_example/main/camera_pinout.h) and [LILYGO camera-bme280](https://github.com/LilyGO/esp32-camera-bme280). LILYGO support initializes only its camera; it does not access the BME280, OLED, or PIR sensor.

## Run the doorbell example

1. Edit the existing definitions in `Settings.h`:

   ```cpp
   #define DOORBELL_BOARD BOARD_AI_THINKER

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


## Use with SinricPro (remote live view)

The **SinricProCamera** example streams to the SinricPro portal and app from anywhere. Signaling runs over the device's SinricPro connection: the viewer's offer arrives as a `getWebRTCAnswer` request together with STUN/TURN servers, and `SinricProWebRTCSession` answers it.

1. Install the SinricPro library version that provides `CameraController::onWebRTCOffer`.
2. In the portal, create a **Camera** device and set Camera Stream Configuration to Board **ESP32**, Streaming Protocol **WebRTC**. On an ESP32-S3, also tick **H.264 video track** so the camera is offered to Amazon Alexa and Google Home.
3. Open **File > Examples > SinricProWebRTC > SinricProCamera**, fill in `Settings.h`, and upload.
4. Tap the camera in the app, or **Preview** in the portal.

```cpp
SinricProWebRTCSession session;   // owns the peer on its own FreeRTOS task

bool onWebRTCOffer(const String &deviceId, const String &offerSdp,
                   const std::vector<SinricProIceServer> &iceServers, String &answerSdp) {
    std::vector<WebRTCIceServer> servers;
    for (const auto &s : iceServers) servers.push_back({s.url, s.username, s.credential});
    return session.handleOffer(offerSdp, servers, answerSdp);  // blocks until ICE gathering is done
}
```

`handleOffer()` returns an answer that already contains every local candidate, because SinricPro signaling is a single offer/answer exchange without trickle ICE. One viewer is served at a time; a new offer replaces the current viewer. Amazon Alexa and Google Home need a native H.264 track, which an ESP32-S3 provides when `Config::h264` is set and the portal's **H.264 video track** setting is ticked; classic ESP32 streams JPEG and stays portal and app only.

Before connecting, viewers send `getCameraCapabilities`; the SinricPro SDK (5.1.0+) answers `{webrtc, webrtcAudio, webrtcVideo}` on its own, and older firmware is asked to update. Viewers ask for a video track only when `webrtcVideo` is reported, which is what keeps older app and portal versions on the JPEG path.

**Viewer controls.** Text messages on the same DataChannel carry JSON controls (binary messages stay JPEG fragments). When the channel opens the camera sends `{"type":"capabilities",...}` and `{"type":"state",...}`; viewers send `{"type":"set","resolution":"SVGA"}`, `fps`, `flash`, `flip`, `mirror` or `autoQuality`. Resolutions are offered up to `Config::maxFrameSize`, which must not exceed the size the camera was initialized with. Set `Config::flashPin` to expose a flash LED (AI-Thinker: GPIO 4).

**Automatic quality.** When frames take longer to send than the frame interval, or stall, the session first lowers the frame rate and then raises JPEG compression, recovering after a run of fast frames. The viewer sees the level in `state.qualityLevel` and `state.effectiveFps`.

**Microphone.** Set `Config::audio = true`, provide `setAudioSource()` (20 ms of 8 kHz PCMU per call) and call `camera.enableWebRTCAudio()` so viewers request an audio track. The examples enable the XIAO ESP32S3 Sense PDM microphone.

**Video track (ESP32-S3).** Set `Config::h264 = true`, give `Config::cameraConfig` the same `camera_config_t` you passed to `WebRTCCamera::begin()`, and call `camera.enableWebRTCVideo()` so viewers offer a video track. The session then re-initialises the camera in YUV422, encodes with esp_h264 on its own task pinned to the second core, and sends H.264 over RTP while the DataChannel carries only the controls. `Config::h264Width` selects a mode: 320 x 240 at about 3 fps, or 640 x 480 at about 2 fps. A viewer with no DataChannel is a smart display and always gets 640 x 480, since Alexa and Google Home refuse anything below 480p. It restores JPEG mode when the viewer leaves. The encoder adds roughly 272 KB of flash and has no prebuilt library for classic ESP32, which keeps the DataChannel path.

## Capabilities and limits

| Feature | Included behavior |
| --- | --- |
| Camera | 640 x 480 JPEG images by default, up to 5 fps, over an encrypted WebRTC data channel |
| Camera (ESP32-S3) | H.264 on a native video track, 320 x 240 at about 3 fps or 640 x 480 at about 2 fps, encoded in software by esp_h264 |
| Browser viewer | Served directly by the board; reassembles and displays JPEG frames |
| Microphone | XIAO Sense onboard PDM microphone, sent as an 8 kHz PCMU/G.711 audio track |
| Controls | Ring, accept, end call, and an open-door command placeholder |
| Signaling | Doorbell: local HTTP with a viewer token. SinricProCamera: SinricPro cloud, with STUN/TURN |

JPEG camera streaming requires the included viewer; it is not a native WebRTC video track. H.264 encoding is available on ESP32-S3 only, where it is capped near 320 x 240 by the software encoder. Speaker playback, two-way audio, and acoustic echo cancellation are not included. Microphones on other board profiles are disabled by default.

**An H.264 video track and an audio track do not run well together.** Measured on a XIAO ESP32S3 Sense: video alone delivers about 3 fps with no loss, but with a PCMU track negotiated the viewer loses roughly two thirds of the video packets and decodes nothing, while the audio itself arrives intact and the device reports every frame as sent. Enlarging `rtp_cfg.send_queue_num` to 128 and `send_pool_size` to 112 kB did not change it. The SinricPro portal and app therefore request audio only when the viewer turns it on. Offer both tracks only if you have verified the combination on your own board.

The example uses direct connections on a trusted LAN. HTTP signaling and the viewer token are unencrypted, although WebRTC media and data transport are encrypted. Its browser candidate adapter assumes a direct LAN connection and does not support reverse proxies or NAT. Remote access requires authenticated HTTPS/WebSocket signaling and suitable ICE/STUN/TURN configuration.

## Library API

```cpp
#include <SinricProWebRTC.h>

SinricProWebRTC peer;
```

The header is named `SinricProWebRTC.h`; the wrapper class is `SinricProWebRTC`.

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
| Core version error | Match the library ZIP to the Arduino core version; rebuilding requires the matching build profile |
| Sketch exceeds available space | Select a partition with a 3 MB application slot |
| Camera fails to initialize (`0x106`) | Check the printed profile and pins. AI-Thinker ESP32-CAM needs `BOARD_AI_THINKER`, not `BOARD_ESP_EYE`; also check the ribbon, power, and PSRAM |
| Viewer cannot connect | Same LAN, correct token, firewall rules, and Wi-Fi client isolation; disconnect a VPN if it prevents LAN ICE connectivity |
| Audio does not play | Use XIAO Sense with `DOORBELL_MIC` enabled; press Play in the browser audio control if autoplay is blocked |

The [HardwareCheck example](examples/HardwareCheck/HardwareCheck.ino) provides camera, crypto, and peer initialization diagnostics. Select its camera profile before uploading.

## PlatformIO

Depend on the registry package, which carries the 3.3.11 archives:

```ini
lib_deps = sinricpro/SinricProWebRTC@^0.3.0
```

For a core other than 3.3.11, point `lib_deps` at that core's release asset instead:

```ini
lib_deps = https://github.com/sinricpro/arduino-esp32-webrtc-lib/releases/download/<version>/SinricProWebRTC-<version>-arduino-3.3.10.zip
```

Use the pinned platform URL from the example below and enable the appropriate PSRAM and application partition settings. `library.json` automatically links the archive for your MCU, and the version guard rejects a mismatched Arduino framework. PlatformIO integration currently targets 3.3.11; the Arduino CLI workflow builds both core versions.

### The bundled example project

Use the provided [PlatformIO project](examples/PlatformIO/platformio.ini). It links this library locally and reuses `Doorbell.ino`, `Settings.h`, and `CameraConfig.h`. Run it from a staged build so the archives match the core under test:

```powershell
python -m platformio run --project-dir build/arduino-3.3.11/SinricProWebRTC/examples/PlatformIO -e esp32cam
python -m platformio run --project-dir build/arduino-3.3.11/SinricProWebRTC/examples/PlatformIO -e xiao_s3_sense
python -m platformio run --project-dir build/arduino-3.3.11/SinricProWebRTC/examples/PlatformIO -e lilygo_camera
```

The project pins **pioarduino 55.03.311**, which supplies Arduino ESP32 **3.3.11**. Use the 3.3.11 library variant with it. The project's build flags select the board profile, overriding the default in `Settings.h`. Append `-t upload` to flash; use `pio device monitor --baud 115200` for logs.

## Build from source

On Windows, install Python **3.12 or newer**, Git, Node.js, and your selected Arduino ESP32 core. In PowerShell from the project root:

```powershell
$coreVersion = '3.3.11' # Or '3.3.10', with that core installed.
python tools/fetch_sources.py --core-version $coreVersion
python tools/build_archives.py --core-version $coreVersion --target esp32
python tools/build_archives.py --core-version $coreVersion --target esp32s3
python tests/archives.py --core-version $coreVersion
python tools/prepare_library.py --core-version $coreVersion
node tests/viewer.test.cjs
python tools/compile_matrix.py --core-version $coreVersion
python tools/package.py --core-version $coreVersion
```

Run one command at a time and stop on errors. The scripts use Arduino's installed SDK and compiler; no separate IDF or WSL setup is needed. `ARDUINO_DIRECTORIES_DATA` can select an isolated Arduino package directory, and `ARDUINO_CLI` can specify the CLI executable.

Each core gets separate sources, build output, archive manifests, and a ZIP with an exact version guard. Builds write archives into `build/` and package them into `dist/`. The repository tracks the 3.3.11 archives under `src/esp32/` and `src/esp32s3/` because Library Manager and the PlatformIO Registry install the tree as-is; refresh them with `tools/refresh_baseline.py` whenever a change alters the built API, or CI rejects the commit. The available adapter, transport, libSRTP, and private Mbed TLS sources are compiled; Espressif's supplied peer-engine binary is included. See [BUILDING.md](BUILDING.md) for setup and validation details.

## Supporting multiple Arduino core versions

[core_profiles.json](tools/core_profiles.json) defines the SDK, compiler, and source revisions for **3.3.11 / IDF 5.5.5** and **3.3.10 / IDF 5.5.4**. Adding a version requires a matching profile, successful compilation, and hardware validation before claiming physical compatibility.

Arduino selects precompiled archives by processor, not core version. Install the ZIP matching your core and keep one variant installed at a time. Removing the guard does not make incompatible binaries safe to use.

## Automated builds and releases

[build.yml](.github/workflows/build.yml) runs on PRs, default-branch pushes (including merges), published releases, and manual dispatch. Separate Windows jobs rebuild each core's ESP32 and ESP32-S3 archives, check crypto isolation, test the viewer, and compile all ten Doorbell profiles plus HardwareCheck. The 3.3.11 job also builds the PlatformIO ESP32 and ESP32-S3 examples.

Publishing a release rebuilds every core version from the tagged commit and attaches each `SinricProWebRTC-<version>-arduino-<core>.zip` to it. A tag that disagrees with `library.properties` fails before any archive is built, and the 3.3.11 job rejects a commit whose tracked archives no longer export what the build defines. Artifacts from ordinary runs expire and need a GitHub login, so use a release for installation. CI does not flash boards; hardware validation is recorded separately.

## Does it use a server?

The current Doorbell example uses your ESP32 as the server. No external cloud server is configured.

| Service | Location |
|---|---|
| Web server and browser viewer | On the ESP32, HTTP port 80 |
| Signaling — exchanging connection details | On the same ESP32, through HTTP endpoints |
| Camera/audio transport | Directly between the ESP32 and your browser over WebRTC |
| STUN server | None configured |
| TURN relay server | None configured |
| SinricPro/Espressif cloud | Not used by this example |

Your board's last observed address was `http://your-ip/`. That address comes from your local router and may change.

The current setup is intended for devices on the same LAN. Internet access would require additional signaling and STUN/TURN configuration.


## License and credits

Based on Espressif's esp-webrtc-solution doorbell demo and `esp_peer` engine. See [LICENSE](LICENSE) for this project's license and [THIRD_PARTY.md](THIRD_PARTY.md) for upstream sources, pinned revisions, and dependency licenses.
