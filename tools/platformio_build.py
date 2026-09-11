"""Link the archive for the selected MCU when PlatformIO loads this library."""
from pathlib import Path

Import("env", "pio_lib_builder")

target = env.BoardConfig().get("build.mcu")
if target not in ("esp32", "esp32s3"):
    raise RuntimeError("SinricProWebRTC provides archives for ESP32 and ESP32-S3 only.")

# Use the library builder's path: SCons changes cwd to this script's directory.
archive_dir = Path(pio_lib_builder.path, "src", target).resolve()
if not (archive_dir / "libsinric_webrtc.a").is_file():
    raise RuntimeError("Missing WebRTC archive. Install a built library ZIP for your Arduino core.")

env.AppendUnique(LIBPATH=[str(archive_dir)], LIBS=["sinric_webrtc"])
