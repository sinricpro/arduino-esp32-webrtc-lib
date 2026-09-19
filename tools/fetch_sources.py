"""Fetch exact sources used by build_archives.py. Requires Git and Python."""
from pathlib import Path
import hashlib
import subprocess
import argparse
import urllib.request
import zipfile
from build_config import add_core_argument, profile, sources_dir, verify_sources

parser = argparse.ArgumentParser(description=__doc__)
add_core_argument(parser)
args = parser.parse_args()
selected = profile(args.core_version)
ROOT = sources_dir(args.core_version)
SOURCES = [
    ('esp-webrtc-solution', 'https://github.com/espressif/esp-webrtc-solution.git', selected['peer_commit']),
    ('esp-idf', 'https://github.com/espressif/esp-idf.git', selected['idf_commit']),
    ('esp-adf-libs', 'https://github.com/espressif/esp-adf-libs.git', selected['adf_commit']),
]

def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()

# Repository root, not build-support/: .gitignore excludes that whole tree, so patches kept there
# would never reach CI and it would silently build stock sources.
PATCHES = ROOT.parents[1] / 'patches'

def apply_patches(path, relative):
    """Re-apply local fixes to a pinned checkout, skipping any already present.

    Without this a local build and CI would compile different sources while reporting the same
    commit, because the commit check below passes either way.
    """
    directory = PATCHES / relative
    if not directory.is_dir():
        return
    for patch in sorted(directory.glob('*.patch')):
        if subprocess.run(['git', '-C', str(path), 'apply', '--reverse', '--check', str(patch)],
                          capture_output=True).returncode == 0:
            print(' ', patch.name, 'already applied')
            continue
        subprocess.check_call(['git', '-C', str(path), 'apply', str(patch)])
        print(' ', patch.name, 'applied')

for relative, url, commit in SOURCES:
    path = ROOT / relative
    if not path.exists():
        path.mkdir(parents=True)
        git(path, 'init')
        git(path, 'remote', 'add', 'origin', url)
        git(path, 'fetch', '--depth', '1', 'origin', commit)
        git(path, 'checkout', '--detach', 'FETCH_HEAD')
    if git(path, 'rev-parse', 'HEAD') != commit:
        raise SystemExit(f'{path}: different checkout; preserve it and choose a clean workspace.')
    print(relative, commit)
    apply_patches(path, relative)
git(ROOT / 'esp-idf', 'submodule', 'update', '--init', '--depth', '1', 'components/mbedtls/mbedtls')
verify_sources(args.core_version)

# esp_h264's software encoder is published only through the component registry, so it is pinned by
# version and archive hash instead of a commit: esp-adf-libs still carries 0.1.1 from 2023, whose
# prebuilt-only encoder has no rate control and takes I420 rather than the camera's YUYV.
h264 = ROOT / 'esp_h264'
if not h264.exists():
    version = selected['h264_version']
    url = ('https://components-file.espressif.com/components/espressif/esp_h264/'
           f'{version}/espressif__esp_h264-v{version}.zip')
    archive = ROOT / 'esp_h264.zip'
    urllib.request.urlretrieve(url, archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != selected['h264_sha256']:
        archive.unlink()
        raise SystemExit(f'esp_h264 {version}: sha256 {digest} does not match the pinned value')
    with zipfile.ZipFile(archive) as package:
        package.extractall(h264)
    archive.unlink()
print('esp_h264', selected['h264_version'])
