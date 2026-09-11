"""Create an Arduino IDE Install ZIP Library archive without build downloads."""
from pathlib import Path
import zipfile
import argparse
from build_config import PROFILES, package_files, library_dir, verify_archive, version_header

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core-version', choices=PROFILES)
args = parser.parse_args()
source = library_dir(args.core_version) if args.core_version else root
if args.core_version:
    for target in ('esp32', 'esp32s3'):
        verify_archive(args.core_version, target)
    if (source / 'src/SinricProWebRTCVersion.h').read_text() != version_header(args.core_version):
        raise SystemExit('Wrong version guard; run tools/prepare_library.py for this core.')
settings = (source / 'examples/Doorbell/Settings.h').read_text()
if '"YOUR_WIFI_SSID"' not in settings or '"YOUR_WIFI_PASSWORD"' not in settings:
    raise SystemExit('Refusing to package Wi-Fi credentials: restore placeholders in Settings.h.')
suffix = '-arduino-' + args.core_version if args.core_version else ''
out = root / f'dist/SinricProWebRTC-0.1.0{suffix}.zip'
out.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for f in package_files(source):
        z.write(f, 'SinricProWebRTC/' + f.relative_to(source).as_posix())
print(out)
