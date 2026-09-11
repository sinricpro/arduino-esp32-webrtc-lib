"""Fetch exact sources used by build_archives.py. Requires Git and Python."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    ('upstream', 'https://github.com/espressif/esp-webrtc-solution.git', 'c8650846b512e6e1375e5f78c1c41619b8d645eb'),
    ('build-support/esp-idf', 'https://github.com/espressif/esp-idf.git', 'b774170ff46c393eeb5e495ea37936038d3f4f4f'),
    ('build-support/esp-adf-libs', 'https://github.com/espressif/esp-adf-libs.git', 'da256e5f462a8e010667d35314a5ba5cdc4a8d9a'),
]

def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()

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
git(ROOT / 'build-support/esp-idf', 'submodule', 'update', '--init', '--depth', '1', 'components/mbedtls/mbedtls')
