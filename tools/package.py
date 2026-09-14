"""Create an Arduino IDE Install ZIP Library archive without build downloads."""
from pathlib import Path
import zipfile
import argparse
from build_config import (add_core_argument, package_files, library_dir, library_version,
                          verify_archive, version_header)

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
add_core_argument(parser)
args = parser.parse_args()
# Only a staged build carries archives; the checkout holds sources and releases hold binaries.
source = library_dir(args.core_version)
for target in ('esp32', 'esp32s3'):
    verify_archive(args.core_version, target)
if (source / 'src/SinricProWebRTCVersion.h').read_text() != version_header(args.core_version):
    raise SystemExit('Wrong version guard; run tools/prepare_library.py for this core.')
settings = (source / 'examples/Doorbell/Settings.h').read_text()
if '"YOUR_WIFI_SSID"' not in settings or '"YOUR_WIFI_PASSWORD"' not in settings:
    raise SystemExit('Refusing to package Wi-Fi credentials: restore placeholders in Settings.h.')
out = root / f'dist/SinricProWebRTC-{library_version()}-arduino-{args.core_version}.zip'
out.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for f in package_files(source):
        z.write(f, 'SinricProWebRTC/' + f.relative_to(source).as_posix())
print(out)
