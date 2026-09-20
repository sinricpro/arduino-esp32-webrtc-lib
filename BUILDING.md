# Building versioned archives

The build profiles in [tools/core_profiles.json](tools/core_profiles.json) select exact dependencies for each Arduino ESP32 core:

| Arduino core | ESP-IDF | SDK package version | Xtensa toolchain |
| --- | --- | --- | --- |
| 3.3.11 | 5.5.5 | 3.3.11 | 2601 |
| 3.3.10 | 5.5.4 | 3.3.10 | 2601 |

These are build configurations, not a claim that every board has been tested physically. See [VALIDATION.md](VALIDATION.md) for hardware results.

## What is rebuilt

The scripts compile available peer adapter and transport sources, libSRTP, and a private Mbed TLS configuration against the selected Arduino SDK. Private crypto symbols are renamed to avoid conflicts with Arduino's TLS libraries. Espressif's supplied `libpeer_default.a` is included as a binary, so this is not a complete source rebuild of every dependency. See [THIRD_PARTY.md](THIRD_PARTY.md) for sources and licenses.

Your sketch, camera configuration, and the Arduino wrapper compile normally. Changes to those files do not require rebuilding the archives.

## Prepare Windows

Install Python **3.12 or newer**, Git, Node.js for viewer tests, and Arduino CLI (standalone or bundled with Arduino IDE). Use a short project path without spaces, because GNU ar MRI scripts are used during archive creation. The scripts use Windows tool executables and do not require a separate ESP-IDF or WSL installation.

Open PowerShell in the project root. Set the CLI path if it is not on PATH:

```powershell
$env:ARDUINO_CLI = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
```

Choose the target core. For local multi-version builds, isolate Arduino packages to avoid replacing your IDE's installed core:

```powershell
$coreVersion = '3.3.10' # Or '3.3.11'.
$env:ARDUINO_DIRECTORIES_DATA = Join-Path (Get-Location) "build-support/arduino-$coreVersion"
$env:ARDUINO_DIRECTORIES_DOWNLOADS = Join-Path (Get-Location) 'build-support/arduino-downloads'
& $env:ARDUINO_CLI core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
& $env:ARDUINO_CLI core install "esp32:esp32@$coreVersion" --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
& $env:ARDUINO_CLI core list
```

Run commands individually and stop on errors. Confirm the selected core is installed. Both the CLI and Python scripts use `ARDUINO_DIRECTORIES_DATA`. Without that variable, the builder uses `%LOCALAPPDATA%/Arduino15`; use the matching core installed there.

## Fetch, build, validate, and package

```powershell
python tools/fetch_sources.py --core-version $coreVersion
python tools/build_archives.py --core-version $coreVersion --target esp32
python tools/build_archives.py --core-version $coreVersion --target esp32s3
python tests/archives.py --core-version $coreVersion
python tools/prepare_library.py --core-version $coreVersion
python -m unittest discover -s tests -p test_build_config.py
node tests/viewer.test.cjs
python tools/compile_matrix.py --core-version $coreVersion
python tools/package.py --core-version $coreVersion
```

The source fetch verifies the pinned commits, including Mbed TLS. Each archive builder verifies the SDK IDF revision and source checkouts, and records its core, processor, dependency profile, source commits, and checksum in `build-info.json`. Staging and packaging reject mismatched profiles or modified archive bytes.

The compile matrix builds all ten Doorbell profiles plus HardwareCheck. Board selection is written into isolated sketch copies so profiles sharing a core configuration can reuse Arduino's compilation cache. These builds never upload firmware.

| Output | Location |
| --- | --- |
| Pinned sources | `build-support/sources-<core>/` |
| Intermediate objects | `build/arduino-<core>/objects/<target>/` |
| Staged library | `build/arduino-<core>/SinricProWebRTC/` |
| Compiler logs | `build/arduino-<core>/compile/*.log` |
| Installable ZIP | `dist/SinricProWebRTC-<version>-arduino-<core>.zip` |

The staged library contains both processor archives, an exact core guard in `SinricProWebRTCVersion.h`, matching Arduino metadata, and a README identifying the variant. The version comes from `library.properties`; a release build stops if it disagrees with the tag.

Keep Wi-Fi placeholders in the distributed example. Configure credentials only in your local sketch copy. For hardware validation, install the generated variant, upload HardwareCheck and Doorbell on the intended board, then test camera, audio where available, connection, disconnect/reconnect, and sustained streaming.

The repository tracks only the 3.3.11 archives, under `src/esp32/` and `src/esp32s3/`, because Library Manager and the PlatformIO Registry install the tree as-is. Every tool that links or packages another core needs a staged build; `--core-version` selects which one.

After a change that alters the built API, refresh the tracked pair from the 3.3.11 staged build and commit it:

```powershell
python tools/refresh_baseline.py
```

`tests/baseline.py` runs in CI on the 3.3.11 job and fails the commit if the tracked archives lack a symbol the fresh build defines, or if `build-info.json` no longer describes them. It compares exported symbols rather than bytes, since `ar` output is not reproducible across runs.

## PlatformIO

`library.json` and `tools/platformio_build.py` link the processor-specific archive. The supplied project in `examples/PlatformIO` pins pioarduino **55.03.311**, supplying Arduino ESP32 **3.3.11**. Use it with the 3.3.11 variant:

```powershell
python -m pip install platformio==6.2.0
python -m platformio run --project-dir build/arduino-3.3.11/SinricProWebRTC/examples/PlatformIO -e esp32cam -e xiao_s3_sense
```

The project reuses the Doorbell sketch rather than maintaining a second implementation. A `lilygo_camera` environment is also provided. The 3.3.10 Arduino ZIP is built separately; the provided PlatformIO project is pinned to 3.3.11 and its version guard intentionally rejects a different variant.

## GitHub Actions

The workflow runs an independent Windows job for each core version on pull requests, default-branch pushes, published releases, and manual dispatch. Each job installs its core, builds both archives, stages the library, runs the checks, and publishes a core-labeled ZIP artifact. A failed matrix job does not cancel the other version. The 3.3.11 job also compiles the PlatformIO ESP32 and ESP32-S3 environments.

Artifacts from non-release runs expire and require a GitHub login, so they serve to check a branch rather than to distribute. CI does not test physical hardware.

## Cutting a release

1. Update `version` in `library.properties` and `library.json`, then commit.
2. Publish a GitHub release whose tag is exactly that version.

The matrix rebuilds every core version from the tagged commit, and a separate `publish` job attaches each `SinricProWebRTC-<version>-arduino-<core>.zip` to the release. Only that job holds a write token; the build jobs stay read-only. A tag disagreeing with `library.properties` fails within the first minute, before any archive is built.

## Add another core version

1. Identify the target core's SDK packages, toolchain, and ESP-IDF commit from Espressif's package index and SDK `versions.txt`. SDK package version numbers need not match core version numbers.
2. Add a profile to `tools/core_profiles.json`, including the exact IDF, Mbed TLS, peer, and ADF commits. Check compatibility of the supplied peer binary. If upstream dependencies change, review copied public headers and redistribution licenses too.
3. If SDK layout, compiler flags, crypto APIs, or configuration changed, adapt the builder. Retain the IDF revision, byte-order, symbol-isolation, and checksum checks.
4. Run the full source-build procedure in an isolated Arduino data directory and resolve compilation and hardware failures. A profile entry alone does not establish compatibility.
5. Add the validated version to the workflow matrix. PlatformIO requires a separately pinned matching platform; do not assume its default framework matches the Arduino IDE core.

Arduino's standard precompiled layout selects by MCU and floating-point ABI, not Arduino core version. Keep one matching library variant installed at a time. See the [Arduino library specification](https://docs.arduino.cc/arduino-cli/library-specification/#precompiled-binaries). Never widen the generated version guard without evidence that the same archives work across those versions.
