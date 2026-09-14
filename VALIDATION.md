# Current build and camera validation

The versioned build scripts rebuilt both processor archives from each selected dependency profile. Archive byte-order and private crypto isolation checks passed for all four archives.

| Arduino core | ESP-IDF | Doorbell profiles | HardwareCheck |
| --- | --- | --- | --- |
| 3.3.11 | 5.5.5 | All 10 compile | Compiles |
| 3.3.10 | 5.5.4 | All 10 compile | Compiles |

The profiles are ESP-EYE, XIAO Sense, Freenove S3, M5Camera A/B, AI-Thinker, WROVER-KIT, ESP32-S3 WROOM with PWDN 38, GOOUUU S3, and LILYGO T-Camera. Per-profile logs are in `build/arduino-<core>/compile/`; local compiled-size results are in `build/versioned-compile-results.json`.

PlatformIO Core 6.2.0 builds passed for ESP32-CAM and XIAO ESP32S3 Sense using pioarduino 55.03.311 / Arduino 3.3.11. These builds linked the freshly rebuilt 3.3.11 archives from the staged library, with the camera configuration owned by the sketch. The workflow passed Actionlint locally. A GitHub-hosted workflow run has not been triggered from this workspace.

Six build-configuration tests passed, covering wrong core/processor/profile, altered archive bytes, and exclusion of PlatformIO build outputs from distribution. The embedded viewer's existing protocol tests passed after formatting.

## LILYGO T-Camera camera fix

The user identified the connected board as LILYGO TTGO T-Camera, ESP32-WROVER-B with OV2640 and OLED. It reports 4 MB flash and 4 MB PSRAM. ESP-EYE and AI-Thinker camera profiles do not match this board. `BOARD_LILYGO_CAMERA` uses the camera pins from LilyGO's camera-bme280 example: XCLK 32, SCCB SDA 13 / SCL 12, PWDN 26.

With Arduino 3.3.11, the installed example initialized the camera and passed the browser test at VGA (640 x 480): 97 rendered JPEG frames in 20 seconds, ring notification, end-call, and reconnect (14 frames). No browser errors were reported. This first camera-fix test used the previously shipped 3.3.11 archive; tests of rebuilt variants are recorded below when completed. No OLED, BME280, PIR, speaker, or actuator functions were enabled.

User settings were preserved, and the selected installed profile was corrected to LILYGO. Pre-change installed files and the board's flash are backed up locally in `build-support/installed-user-edits.zip` and `build/device-backup/com14-before-camera-fix.bin`; they are excluded from library packages.

## Rebuilt 3.3.10 hardware result

The 3.3.10 / IDF 5.5.4 firmware linked the new ESP32 archive and passed the same LILYGO VGA browser test: 97 frames in 20 seconds, ring, end-call, and reconnect with 14 frames, with no browser errors. An initial upload failed with an esptool serial error; retrying at 115200 baud succeeded and the flash hash was verified. Local results are in `build/lilygo-browser-3.3.10.json`.

## Rebuilt 3.3.11 hardware result

After testing 3.3.10, the board and installed Arduino library were returned to the newly rebuilt 3.3.11 variant. The final LILYGO VGA browser test passed with 97 frames in 20 seconds, ring, end-call, and reconnect with 14 frames; no browser errors occurred. Flash hashes were verified after upload. Local results are in `build/lilygo-browser-3.3.11-rebuilt.json`.

These checks establish short live-streaming operation on this LILYGO board for both variants. Microphone quality, long-duration stability, remote ICE/NAT traversal, and the other untested physical board variants remain outside these hardware results.

## Earlier baseline results

The following records describe the earlier six-profile release and its original archive checksums; newly rebuilt variants carry their own `build-info.json` files.

# Validation — 2026-09-11

## Arduino compilation

Built with the installed Arduino ESP32 **3.3.11** (IDF **5.5.5**) and its Windows
Xtensa **2601** toolchain. All builds link the packaged architecture-specific archive.

| Doorbell profile | Result | Firmware bytes | Static RAM bytes |
| --- | --- | ---: | ---: |
| XIAO ESP32S3 Sense, onboard microphone enabled | PASS | 1,476,858 | 74,288 |
| ESP-EYE | PASS | 1,511,956 | 75,008 |
| Freenove ESP32-S3-WROOM | PASS | 1,450,754 | 73,328 |
| M5Camera A | PASS | 1,511,676 | 75,008 |
| M5Camera B | PASS | 1,511,676 | 75,008 |
| AI-Thinker pinout, distributed example | PASS | 1,511,676 | 75,008 |

Static RAM figures exclude runtime heap, camera buffers and connection allocations.
The AI-Thinker local device build has local Wi-Fi settings and uses 1,511,660 bytes;
the distributed example retains placeholders. Its slightly different size is expected.
The HardwareCheck example also compiles (1,476,352 bytes).

`tools/compile_matrix.py` reproduces the six Doorbell profile builds without flashing.
Raw logs on the development PC are under `build/` and `build-support/` (not in the ZIP).

## Physical board checks

Connected over COM6: ESP32-D0WD-V3 revision 3.0, 4 MB flash, 2 MB PSRAM.
Although initially identified as ESP-EYE, this board's camera did not initialize
with the ESP-EYE pinout. It initialized successfully using **AI-Thinker ESP32-CAM
pins**. This establishes the compatible pinout, not the manufacturer's identity.

The diagnostic firmware passed:

- Private AES, SHA-256, SHA-1 and CTR-DRBG known-answer self-tests (return code 0).
- SRTP initialization, including its built-in cipher/authentication checks (return code 0).
- Ten consecutive camera captures with JPEG start markers, about 3.2–5.5 KB per frame.
- DTLS self-signed certificate generation (return code 0, about 125 ms).
- Peer creation, starting a connection, and generation of 481-byte data-channel SDP.
- Peer teardown without a reset or reported exception during the diagnostic run.

The full original 4 MB flash was backed up before any writes. The backup remains
locally at `build/device-backup/esp-eye-before-webrtc.bin` and is deliberately
excluded from the library ZIP. SHA-256:
`8f27b973abbee2b8c96c16f525fdee379fec57f546465887678260f69357d22b`.

The final Doorbell firmware with AI-Thinker pins and the supplied Wi-Fi settings
was uploaded and its flash hash verified. It joined Wi-Fi at `192.168.1.151`.

## Live browser test

Microsoft Edge, automated through Playwright, connected to the physical ESP32:

- ICE pairing, DTLS handshake and SCTP data-channel establishment: **PASS**.
- **98 complete JPEG frames rendered in 20 seconds**, approximately 4.9 fps.
- 393 binary messages / 382,184 payload bytes received during that interval.
- Serial `r` produced the browser ring notification: **PASS**.
- Open-door command returned `NO_LOCK_CONFIGURED`: **PASS**; no actuator was connected.
- End call stopped image delivery: **PASS**.
- Disconnect and reconnect in the same page: **PASS**, with 15 further frames in 3 seconds.
- Unhandled browser errors: **none**.

The PC's Cloudflare WARP interface was the browser's default ICE interface.
An ordinary headless browser advertised only that VPN interface and could not pair.
The successful test used `--all-interfaces`: an isolated browser with a **fake**
microphone permission, so WebRTC enumerated its real LAN interface as well. No real
microphone was recorded and no OS/VPN settings were changed. For normal use, disable
the VPN if it prevents the browser from advertising/reaching the LAN interface.

Raw results are in `build/browser-final.json`, with the rendered-frame screenshot
in `build/browser-camera.png`; those private local artifacts are not in the ZIP.

**Not yet verified:** long-duration streaming/memory stability, microphone audio
quality, remote STUN/TURN/NAT traversal, or physical operation of the requested
ESP-EYE/S3/M5 boards. Their build profiles passed compilation; the live hardware
result applies specifically to the connected AI-Thinker-compatible pinout board.

## Host checks

- `node tests/viewer.test.cjs`: complete JPEG fragment reassembly, missing fragments,
  stale frame IDs, oversized allocations, short messages and ring notifications.
- `python tests/archives.py`: all 149 ELF objects in each archive are 32-bit
  little-endian, public peer symbols exist, DTLS-SRTP symbols exist, and private
  Mbed TLS/PSA/SRTP symbols do not collide with Arduino's symbols.
- Pinned source checkouts validated by `tools/fetch_sources.py`.

Archive SHA-256 values:

| Target | SHA-256 |
| --- | --- |
| ESP32 | `6cee4f94a4dc6ade9528dc4de038ad781dfaaf73647191f93a779b7347573160` |
| ESP32-S3 | `892a0d923385529178f3de84e44c192696035570018afb1cc5888d654b4e2ab1` |

## Continue hardware validation

Reconnect the board, select its serial port, and capture the boot log:

```powershell
python tools/serial_capture.py --port COM6 --reset --seconds 25
```

Open the printed IP address in the browser, or run the automated smoke test with
Python Playwright and Microsoft Edge installed:

```powershell
python tests/hardware_browser.py http://BOARD_IP --token change-this-token
```

Add `--all-interfaces` for the test PC's VPN routing constraint and `--serial-port
COM6` to exercise the ring command. The smoke test connects, accepts a call,
requires successfully rendered JPEG frames, records WebRTC transport statistics,
checks commands and stream stop, and reconnects. It is intentionally
opt-in and does not scan the network or upload firmware.

## SinricPro integration (SinricProCamera example) — pending hardware checks

| Check | Status |
| --- | --- |
| Example compiles for ESP32 (AI-Thinker) and ESP32-S3 (XIAO profile) on 3.3.11 | PASS: 1,705,572 / 1,640,754 bytes, huge_app partition |
| Portal viewer on the same LAN renders frames | PASS (2026-09-13, AI-Thinker ESP32-CAM at QVGA; see findings below) |
| App on mobile data (CGNAT) connects via `srflx` or `relay` candidate | Not yet verified |
| Forced relay (`iceTransportPolicy: 'relay'`) over TURN UDP 3478 | Not yet verified |
| TURNS over TCP 443 on a UDP-blocked network | Not yet verified |
| Offer-to-answer time under 4 s with TURN allocation | Not yet verified |
| Classic ESP32: heap stable for 30 min with TLS websocket + DTLS active | Not yet verified |
| Second viewer replaces the first cleanly | Not yet verified |
| Controls: resolution (QVGA..SVGA), fps, flash (AI-Thinker GPIO 4), flip, mirror apply live | Not yet verified |
| Automatic quality steps down on a throttled link and recovers | Not yet verified |
| XIAO ESP32S3 Sense microphone audible in portal and app | Not yet verified (mic path compiles) |
| Firmware before SDK 5.1.0 shows the update-firmware message | Not yet verified |

## Classic ESP32 bring-up findings — 2026-09-13

Portal live view works on an AI-Thinker ESP32-CAM, but only after two constraints were found. Both
present as the same misleading symptom and neither reports a useful error on its own.

**Wi-Fi signal is a hard requirement.** At −85 to −88 dBm the DTLS handshake never completes. The
Wi-Fi driver's transmit buffers are a fixed count released only when a frame is acknowledged, so a
weak link leaves none free and `sendto()` returns `ENOMEM` even for 88-byte STUN packets — while
free heap reads ~49 kB with a 28 kB largest block, so every memory metric looks healthy. At −79 to
−85 dBm the same board streamed for 90 s and lost 1 frame of roughly 450. Target **−75 dBm or
better**; `SinricProWebRTCSession` now appends the reading to the viewer's error message below that
threshold. An ESP32-CAM on its PCB trace antenna, 10–15 m from the access point through walls,
measured −85 dBm and did not work.

**Data-channel caches must stay small on classic ESP32.** Once Wi-Fi and the SinricPro TLS socket
are up, a single ~28 kB contiguous internal block remains. The former 16 kB send / 8 kB receive
caches consumed it, leaving the Wi-Fi driver unable to allocate transmit buffers at all. The example
now uses 6 kB / 3 kB, which lifted the observed heap floor from 31.6 kB to 49.7 kB and allowed the
handshake to complete. `rtp_cfg.send_pool_size` is also reduced when no audio track is negotiated —
it must never be set to `0`, which selects esp_peer's 400 kB default.

Symptoms worth recognising: the portal shows one frozen frame while the device logs `streaming:
yes`, or the data channel stays at `connecting` with `dtlsState=connecting` in `chrome://webrtc-internals`.

**Vendor patch.** `patches/esp-webrtc-solution/esp_peer-udp-errno.patch` fixes an upstream defect
where `select()` overwrote `errno` between retries, so the `ENOBUFS`/`ENOMEM` retry test read an
unrelated value, and adds a rate-limited log to the otherwise silent `-200` return.
`tools/fetch_sources.py` applies it after checkout and `build-info.json` records it under `patches`,
so a patched archive is no longer indistinguishable from a stock one. Reported upstream on
[esp-webrtc-solution#75](https://github.com/espressif/esp-webrtc-solution/issues/75#issuecomment-5654157318).
