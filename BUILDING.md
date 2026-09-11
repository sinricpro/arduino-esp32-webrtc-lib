# Building from source

This guide covers rebuilding the archives on Windows and adapting the build to another Arduino ESP32 core version.

**Currently supported: Arduino ESP32 3.3.11 only.** The scripts and CI are pinned to that version. Additional versions require porting and validation; there is currently no `--core-version` option.

## What gets compiled

The builder compiles the available peer adapter and transport sources, libSRTP, and private Mbed TLS against Arduino's installed ESP-IDF SDK. It supplies hardware randomness and a monotonic clock, then renames private crypto symbols to avoid collisions with Arduino's TLS libraries.

The resulting archive also includes Espressif's supplied `libpeer_default.a`. That peer engine is consumed as a binary, so this is not a complete source rebuild of every dependency. Compatibility with another SDK also depends on that upstream binary. See [THIRD_PARTY.md](THIRD_PARTY.md) for revisions and licenses.

Arduino compiles the wrapper and your sketch normally. Camera configuration changes do not require rebuilding the archives.

## Prepare Windows

Install Python **3.12 or newer**, Git, and **esp32 by Espressif Systems 3.3.11** through Arduino Boards Manager. Install Node.js for the viewer tests. Use a short project path without spaces, such as `C:\dev\sinricpro-webrtc-lib`, because the archive builder uses GNU ar MRI scripts.

Open PowerShell in the project root. Packages must be installed under `%LOCALAPPDATA%\Arduino15`. No separate ESP-IDF installation, WSL, or `IDF_PATH` is needed. The scripts use Windows executables; these instructions do not apply to Linux/WSL.

For the commands below, select your Arduino CLI executable. The normal Arduino IDE installation provides one here:

```powershell
$env:ARDUINO_CLI = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
& $env:ARDUINO_CLI core list
```

Change the path if you installed the IDE elsewhere or use standalone Arduino CLI. Confirm that `core list` shows `esp32:esp32` version `3.3.11`.

## Fetch and build

Run each command from the project root and stop if it reports an error:

```powershell
python tools/fetch_sources.py
python tools/build_archives.py --target esp32
python tools/build_archives.py --target esp32s3
```

The fetch script downloads pinned upstream commits and the matching IDF Mbed TLS submodule. It refuses to reuse checkouts at different commits; use a fresh project copy when experimenting with other revisions.

| Component | Current selection |
| --- | --- |
| Arduino ESP32 core | 3.3.11 |
| SDK packages | `tools/esp32-libs/3.3.11` and `tools/esp32s3-libs/3.3.11` |
| ESP-IDF | 5.5.5, commit `b774170ff46c393eeb5e495ea37936038d3f4f4f` |
| Xtensa toolchain | `tools/esp-x32/2601/bin` |

Successful builds replace `src/esp32/libsinric_webrtc.a` and `src/esp32s3/libsinric_webrtc.a`. Each target also receives a `build-info.json` recording versions, source commits, symbol count, and SHA-256. Intermediate files go under `build/`.

## Validate and package

```powershell
python tests/archives.py
node tests/viewer.test.cjs
python tools/compile_matrix.py
& $env:ARDUINO_CLI compile --fqbn 'esp32:esp32:esp32:PSRAM=enabled,PartitionScheme=huge_app' --library . --build-path build/hardwarecheck examples/HardwareCheck
python tools/package.py
```

The matrix compiles all six Doorbell profiles and saves logs as `build/matrix-*.log`. Archive checks verify byte order, required symbols, and private crypto isolation. Compilation does not establish runtime compatibility: also run HardwareCheck and Doorbell on hardware, checking camera, audio where available, connection, disconnect, and reconnect. See [VALIDATION.md](VALIDATION.md) for the existing results and test procedure.

Install `dist/SinricProWebRTC-0.1.0.zip` through **Sketch > Include Library > Add .ZIP Library**, replacing the previous library copy. Keep Wi-Fi placeholders in the distributed example; packaging rejects substituted credentials.

## Port to another core version

Treat each core version as a separate build configuration. Changing the version guard alone does not make existing binaries compatible.

1. Start in a separate project copy and install the target core in an isolated build environment. Record its actual SDK packages, compiler, and ESP-IDF revision from package metadata and SDK `versions.txt`. SDK package versions do not necessarily equal the core version.
2. Update the IDF commit in `tools/fetch_sources.py` to match the SDK, including its Mbed TLS submodule. Check whether the pinned peer and libSRTP revisions support it. If those dependencies change, update their pins, public headers, and license records together.
3. Adapt `tools/build_archives.py`: SDK and toolchain paths, executable names, expected IDF revision, include paths, compiler flags, and build metadata. Review private Mbed TLS configuration, port functions, and symbol renaming for API changes. Retain the byte-order and crypto-isolation checks.
4. Set the exact target core in `src/SinricProWebRTC.h` and update its diagnostic. Update the toolchain path in `tests/archives.py`, any changed board options in `tools/compile_matrix.py`, and the required version in `library.properties` and documentation.
5. Rebuild both archives and run all compilation and hardware checks with that core. If the supplied peer binary is incompatible, an appropriate upstream binary or upstream changes are required.
6. Package the validated result separately, label it with its core version, and record the tested SDK, compiler, commits, and hardware results.

After the scripts have explicit configurations for each validated version, CI can run them as a version matrix. The current workflow still builds only 3.3.11; adding matrix values alone would continue selecting the hard-coded SDK.

## Distribute multiple versions

Arduino selects precompiled archives by MCU and, where applicable, floating-point ABI. There is no standard core-version directory selector: adding a directory such as `src/esp32/3.3.11/` does not enable automatic selection. See the [Arduino library specification](https://docs.arduino.cc/arduino-cli/library-specification/#precompiled-binaries).

Provide a separate installable ZIP for each validated core. Keep the normal `SinricProWebRTC/src/esp32/` and `src/esp32s3/` layout inside each ZIP, with a matching exact version guard. Install one variant at a time to avoid duplicate-library selection.

After packaging the existing 3.3.11 build, label it explicitly:

```powershell
Copy-Item dist/SinricProWebRTC-0.1.0.zip dist/SinricProWebRTC-0.1.0-arduino-3.3.11.zip
```

A single archive set may eventually support several core versions if compatibility is established for each version before widening the guard.
