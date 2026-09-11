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
