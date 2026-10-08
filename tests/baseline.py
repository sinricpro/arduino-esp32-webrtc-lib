"""Check the committed baseline archives against a fresh build of the same core.

Library Manager serves the repository tree, not a release ZIP, so these archives are what most
users link against. Nothing else would catch a commit that adds sources without refreshing them.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
from build_config import DEFAULT_CORE, TARGETS, binutil, library_dir, profile


def defined_symbols(archive, target):
    """Global definitions; undefined references print two fields rather than three."""
    nm = binutil(DEFAULT_CORE, target, 'nm')
    output = subprocess.check_output([str(nm), '-g', str(archive)], text=True)
    return {fields[2] for fields in map(str.split, output.splitlines()) if len(fields) == 3}


for target in TARGETS:
    committed = root / 'src' / target / 'libsinric_webrtc.a'
    manifest = json.loads(committed.with_name('build-info.json').read_text())
    if manifest.get('arduino_core') != DEFAULT_CORE or manifest.get('target') != target:
        raise SystemExit(f'{committed}: build-info.json describes another core or target.')
    if manifest.get('profile') != profile(DEFAULT_CORE):
        raise SystemExit(f'{committed}: built against a superseded core profile; refresh it.')
    if manifest.get('sha256') != hashlib.sha256(committed.read_bytes()).hexdigest():
        raise SystemExit(f'{committed}: checksum disagrees with build-info.json.')
    fresh = library_dir(DEFAULT_CORE) / 'src' / target / 'libsinric_webrtc.a'
    # Byte equality would require a reproducible `ar`; the exported API is what the headers promise.
    missing = defined_symbols(fresh, target) - defined_symbols(committed, target)
    if missing:
        raise SystemExit(f'{committed}: stale baseline missing {len(missing)} symbols, including '
                         f'{sorted(missing)[:5]}; rebuild and commit it.')
    print(f'PASS {target}: baseline matches the {DEFAULT_CORE} profile and the built API')
