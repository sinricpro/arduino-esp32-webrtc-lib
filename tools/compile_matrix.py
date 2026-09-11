"""Compile the distributed Doorbell example for each board; never uploads."""
from pathlib import Path
import os
import shutil
import subprocess
import argparse
import re
from build_config import PROFILES, library_dir, workspace, verify_archive
root = Path(__file__).resolve().parents[1]
cli = os.environ.get('ARDUINO_CLI') or shutil.which('arduino-cli') or str(
    Path(os.environ['LOCALAPPDATA']) / 'Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core-version', choices=PROFILES)
options = parser.parse_args()
library = library_dir(options.core_version) if options.core_version else root
build_root = workspace(options.core_version) / 'compile' if options.core_version else root / 'build'
if options.core_version:
    for target in ('esp32', 'esp32s3'):
        verify_archive(options.core_version, target)
boards = [
    ('xiao', 2, 'XIAO_ESP32S3:PSRAM=opi'),
    ('esp-eye', 1, 'esp32:PSRAM=enabled,PartitionScheme=huge_app'),
    ('freenove', 3, 'esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=huge_app'),
    ('m5camera-a', 4, 'esp32:PSRAM=enabled,PartitionScheme=huge_app'),
    ('m5camera-b', 5, 'esp32:PSRAM=enabled,PartitionScheme=huge_app'),
    ('ai-thinker', 6, 'esp32:PSRAM=enabled,PartitionScheme=huge_app'),
    ('wrover-kit', 7, 'esp32:PSRAM=enabled,PartitionScheme=huge_app'),
    ('s3-wroom', 8, 'esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=huge_app'),
    ('s3-goouuu', 9, 'esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=huge_app'),
    ('lilygo-camera', 10, 'esp32:PSRAM=enabled,PartitionScheme=huge_app'),
]
for name, profile, fqbn in boards:
    path = build_root / ('matrix-' + name)
    path.mkdir(parents=True, exist_ok=True)
    # Select the board in the sketch, so identical core builds can share Arduino's cache.
    sketch = build_root / 'sketches' / name / 'Doorbell'
    shutil.copytree(library / 'examples/Doorbell', sketch, dirs_exist_ok=True)
    settings = sketch / 'Settings.h'
    settings.write_text(re.sub(r'#define DOORBELL_BOARD BOARD_\w+',
                              f'#define DOORBELL_BOARD {profile}', settings.read_text()))
    args = [str(cli), 'compile', '--fqbn', 'esp32:esp32:' + fqbn, '--library', str(library),
            '--build-path', str(path), str(sketch)]
    print('Compiling', name, flush=True)
    log_path = path.parent / (path.name + '.log')
    with log_path.open('w') as log:
        result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log_path.read_text()); raise SystemExit(result.returncode)
    print(log_path.read_text(), flush=True)

# The diagnostic sketch also uses the camera and public peer API.
path = build_root / 'hardwarecheck'
path.mkdir(parents=True, exist_ok=True)
with (build_root / 'hardwarecheck.log').open('w') as log:
    result = subprocess.run([str(cli), 'compile', '--fqbn',
        'esp32:esp32:esp32:PSRAM=enabled,PartitionScheme=huge_app',
        '--library', str(library), '--build-path', str(path),
        str(library / 'examples/HardwareCheck')], stdout=log, stderr=subprocess.STDOUT)
print((build_root / 'hardwarecheck.log').read_text(), flush=True)
raise SystemExit(result.returncode)
