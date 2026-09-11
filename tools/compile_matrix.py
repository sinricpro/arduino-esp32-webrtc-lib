"""Compile the distributed Doorbell example for each board; never uploads."""
from pathlib import Path
import os
import shutil
import subprocess
root = Path(__file__).resolve().parents[1]
cli = os.environ.get('ARDUINO_CLI') or shutil.which('arduino-cli') or str(
    Path(os.environ['LOCALAPPDATA']) / 'Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe')
boards = [
    ('xiao', 2, 'XIAO_ESP32S3:PSRAM=opi'),
    ('esp-eye', 1, 'esp32:PSRAM=enabled,PartitionScheme=huge_app'),
    ('freenove', 3, 'esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=huge_app'),
    ('m5camera-a', 4, 'esp32:PSRAM=enabled,PartitionScheme=huge_app'),
    ('m5camera-b', 5, 'esp32:PSRAM=enabled,PartitionScheme=huge_app'),
    ('ai-thinker', 6, 'esp32:PSRAM=enabled,PartitionScheme=huge_app'),
]
for name, profile, fqbn in boards:
    path = root / 'build' / ('matrix-' + name)
    path.mkdir(parents=True, exist_ok=True)
    args = [str(cli), 'compile', '--fqbn', 'esp32:esp32:' + fqbn, '--library', str(root),
            '--build-property', f'compiler.cpp.extra_flags=-DDOORBELL_BOARD={profile}',
            '--build-path', str(path), str(root / 'examples/Doorbell')]
    print('Compiling', name, flush=True)
    log_path = path.parent / (path.name + '.log')
    with log_path.open('w') as log:
        result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log_path.read_text()); raise SystemExit(result.returncode)
    print(log_path.read_text(), flush=True)
