"""Create an Arduino IDE Install ZIP Library archive without build downloads."""
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[1]
settings = (root / 'examples/Doorbell/Settings.h').read_text()
if '"YOUR_WIFI_SSID"' not in settings or '"YOUR_WIFI_PASSWORD"' not in settings:
    raise SystemExit('Refusing to package Wi-Fi credentials: restore placeholders in Settings.h.')
out = root / 'dist/SinricProWebRTC-0.1.0.zip'
out.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for name in ['library.properties', 'README.md', 'BUILDING.md', 'LICENSE', 'THIRD_PARTY.md', 'VALIDATION.md', 'src', 'examples', 'LICENSES', 'tools', 'tests']:
        p = root / name
        for f in sorted(p.rglob('*')) if p.is_dir() else [p]:
            if f.is_file() and '__pycache__' not in f.parts:
                z.write(f, 'SinricProWebRTC/' + f.relative_to(root).as_posix())
print(out)
