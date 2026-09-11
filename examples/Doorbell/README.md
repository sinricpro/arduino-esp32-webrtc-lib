# WebRTC Doorbell

Stream camera images from an ESP32 or ESP32-S3 to a browser on your local network. The board serves the viewer and supports ring, accept, and end-call controls. XIAO ESP32S3 Sense can also send its onboard microphone audio.

## Requirements

- SinricProWebRTC installed, with an archive variant matching your Arduino ESP32 core: **3.3.11 or 3.3.10**.
- A supported camera board with PSRAM enabled and an application partition of at least **3 MB**.
- Chrome or Edge on the same LAN as the board.

See the [library README](../../README.md) for installation and [build guide](../../BUILDING.md) for compiling archives.

## Choose your camera board

Edit `DOORBELL_BOARD` in [Settings.h](Settings.h). Select the camera wiring for your actual board; the processor name alone does not identify its pinout.

| Camera board | Profile | Arduino IDE board |
| --- | --- | --- |
| AI-Thinker ESP32-CAM | `BOARD_AI_THINKER` (default) | ESP32 Dev Module |
| ESP-EYE | `BOARD_ESP_EYE` | ESP32 Dev Module |
| XIAO ESP32S3 Sense | `BOARD_XIAO_S3_SENSE` | XIAO_ESP32S3 |
| Freenove ESP32-S3 camera board | `BOARD_FREENOVE_S3` | ESP32S3 Dev Module |
| M5Camera model A | `BOARD_M5CAMERA` | ESP32 Dev Module |
| M5Camera model B | `BOARD_M5CAMERA_B` | ESP32 Dev Module |
| ESP-WROVER-KIT | `BOARD_WROVER_KIT` | ESP32 Dev Module |
| ESP32-S3 WROOM camera wiring with PWDN GPIO38 | `BOARD_ESP32S3_WROOM` | ESP32S3 Dev Module |
| GOOUUU ESP32-S3 camera wiring | `BOARD_ESP32S3_GOOUUU` | ESP32S3 Dev Module |
| LILYGO TTGO T-Camera, ESP32-WROVER-B / OV2640 | `BOARD_LILYGO_CAMERA` | ESP32 Dev Module |

For the LILYGO T-Camera:

```cpp
#define DOORBELL_BOARD BOARD_LILYGO_CAMERA
```

This profile supports the camera only. It does not initialize the OLED, BME280, or PIR sensor. A LILYGO board with an ESP32-WROVER module needs the LILYGO profile, rather than the separate ESP-WROVER-KIT pinout.

Enable **PSRAM** for classic ESP32 boards. Use **OPI PSRAM** for XIAO Sense and the Freenove N8R8 variant; match the setting to the actual module on other S3 boards. Select the correct flash size and **Huge APP (3MB No OTA/1MB SPIFFS)** on generic boards. XIAO's default 8 MB layout provides a 3 MB application slot.

## Configure and upload

1. Open `Doorbell.ino` in Arduino IDE.
2. Select your camera profile in `Settings.h` and edit the existing values:

   ```cpp
   static const char WIFI_SSID[] = "YOUR_WIFI_SSID";
   static const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
   static const char VIEWER_TOKEN[] = "your-viewer-token";
   ```

3. Select the Arduino board, memory settings, and serial port, then upload.
4. Open Serial Monitor at **115200 baud**. Check the printed camera profile and pins.
5. Open the printed `http://<board-ip>/` address in Chrome or Edge.
6. Enter the viewer token, click **Connect**, then **Accept call**.

Keep credentials in your local sketch copy. The distributed example contains placeholders.

## Viewer controls

| Control | Behavior |
| --- | --- |
| Connect | Establishes the WebRTC connection |
| Accept call | Starts camera streaming and the configured microphone |
| End call | Stops streaming while keeping the connection available |
| Disconnect | Releases the connection |
| Open door | Returns `NO_LOCK_CONFIGURED`; no actuator is driven |
| Serial Monitor `r` | Sends a ring notification to the connected viewer |

Only one viewer can connect at a time. Use **Disconnect** before switching viewers. An abandoned session expires after 15 seconds without viewer polling.

To connect a physical ring button, set `RING_BUTTON_PIN` in `Settings.h` to an unused GPIO and wire the button between that pin and GND. The input uses an internal pull-up. Avoid camera, microphone, flash, and PSRAM pins; the default `-1` disables the physical button.

## Customize the camera

[CameraConfig.h](CameraConfig.h) belongs to the sketch. It contains editable pin mappings and capture settings. The default is **VGA (640 x 480), JPEG quality 16**, with two PSRAM frame buffers. The example limits transmission to approximately **5 fps**.

You can adjust the configuration in `setup()` before initialization:

```cpp
CameraSetup::prepare(cameraBoard());
camera_config_t cameraConfig = CameraSetup::config(cameraBoard());
cameraConfig.frame_size = FRAMESIZE_QVGA; // 320 x 240
cameraConfig.jpeg_quality = 16;
esp_err_t result = WebRTCCamera::begin(cameraConfig);
```

Alternatively, supply your own fully populated `camera_config_t` for a camera supported by the ESP32 camera driver. Perform any board-specific GPIO setup before calling `begin()`. No library changes or archive rebuild are required.

Keep `PIXFORMAT_JPEG`: the viewer receives JPEG fragments over an encrypted data channel and displays them on a canvas. Frames larger than **128 KiB** are dropped. Choose resolution and quality accordingly. This is not an H.264 or native WebRTC video track.

## Microphone

`DOORBELL_MIC` defaults to enabled only for XIAO ESP32S3 Sense. Its PDM microphone uses GPIO42 for clock and GPIO41 for data. The example captures at 16 kHz and sends an 8 kHz PCMU/G.711 audio track.

To disable it, define this before the existing `#ifndef DOORBELL_MIC` block in `Settings.h`:

```cpp
#define DOORBELL_MIC 0
```

Other camera profiles run without microphone audio. Speaker playback and two-way audio are not implemented. If browser autoplay is blocked, press Play in the audio control.

## PlatformIO

The [PlatformIO project](../PlatformIO/platformio.ini) reuses this sketch and its settings. It pins pioarduino 55.03.311 / Arduino ESP32 **3.3.11**, so use the matching library variant.

From the library root, build one of the provided environments:

```powershell
pio run --project-dir examples/PlatformIO -e esp32cam
pio run --project-dir examples/PlatformIO -e xiao_s3_sense
pio run --project-dir examples/PlatformIO -e lilygo_camera
```

The environment's build flags override `DOORBELL_BOARD` in `Settings.h`. Upload with `-t upload` added to the selected command. Close any other Serial Monitor before uploading or opening the port in PlatformIO.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| Camera probe fails with `0x106` | Confirm the printed profile matches the physical board, including PWDN and SCCB pins. Check camera ribbon seating and power. For LILYGO T-Camera, select `BOARD_LILYGO_CAMERA`. |
| Camera initialization reports insufficient memory | Enable the correct PSRAM mode and check the reported PSRAM size. |
| Core-version compilation error | Install the library ZIP matching the Arduino ESP32 core selected in Boards Manager. |
| Sketch exceeds application space | Select a partition with at least a 3 MB application slot. |
| Serial port is busy | Close other serial monitors or programs using the port. |
| Upload fails intermittently | Check USB cable and power; retry at a lower upload speed such as 115200. |
| Viewer cannot connect | Check the token, same-LAN connectivity, firewall, and Wi-Fi client isolation. A VPN may prevent the browser from using its LAN interface. |
| Connected viewer shows no images | Click Accept call; verify JPEG format and the 128 KiB frame limit. |

This example uses direct LAN HTTP signaling. The viewer token and signaling are unencrypted, although WebRTC media and data transport are encrypted. Use it on a trusted LAN. Remote access, reverse proxies, and NAT traversal require a different signaling setup.

For camera and crypto diagnostics, see [HardwareCheck](../HardwareCheck/HardwareCheck.ino). For tested boards and limitations, see [VALIDATION.md](../../VALIDATION.md).
