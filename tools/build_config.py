"""Shared version selection and isolated paths for archive builds."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PROFILES = json.loads((ROOT / 'tools/core_profiles.json').read_text())
DEFAULT_CORE = '3.3.11'
PACKAGE_CONTENTS = ['library.properties', 'library.json', 'README.md', 'BUILDING.md', 'LICENSE',
                    'THIRD_PARTY.md', 'VALIDATION.md', 'src', 'examples',
                    'LICENSES', 'tools', 'tests']


def package_files(root):
    """Enumerate deliverable files without traversing build trees or linked libraries."""
    for name in PACKAGE_CONTENTS:
        path = root / name
        if path.is_file():
            yield path
        elif path.is_dir():
            for directory, dirs, files in os.walk(path, followlinks=False):
                dirs[:] = sorted(d for d in dirs if d not in ('.pio', '.git', '__pycache__')
                                 and not (Path(directory) / d).is_symlink())
                for filename in sorted(files):
                    file = Path(directory) / filename
                    if not file.is_symlink():
                        yield file


def add_core_argument(parser):
    parser.add_argument('--core-version', choices=PROFILES, default=DEFAULT_CORE)


def profile(core):
    return PROFILES[core]


def data_dir():
    return Path(os.environ.get('ARDUINO_DIRECTORIES_DATA') or
                Path(os.environ['LOCALAPPDATA']) / 'Arduino15').resolve()


def workspace(core):
    return ROOT / 'build' / ('arduino-' + core)


def library_dir(core):
    return workspace(core) / 'SinricProWebRTC'


def sources_dir(core):
    return ROOT / 'build-support' / ('sources-' + core)


def source_revisions(core):
    p = profile(core)
    return {'esp-webrtc-solution': p['peer_commit'], 'esp-idf': p['idf_commit'],
            'esp-adf-libs': p['adf_commit'],
            'esp-idf/components/mbedtls/mbedtls': p['mbedtls_commit']}


def verify_sources(core):
    for relative, revision in source_revisions(core).items():
        actual = subprocess.check_output(
            ['git', '-C', str(sources_dir(core) / relative), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != revision:
            raise ValueError(f'{relative}: expected {revision}, found {actual}')


def version_header(core):
    version = ', '.join(core.split('.'))
    return (f'#pragma once\n// Generated for the bundled archive ABI.\n'
            f'#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL({version})\n'
            f'#error "This SinricPro WebRTC archive requires Arduino ESP32 core {core}. Install the matching library ZIP."\n'
            '#endif\n')


def verify_archive(core, target):
    archive = library_dir(core) / 'src' / target / 'libsinric_webrtc.a'
    manifest = json.loads(archive.with_name('build-info.json').read_text())
    expected = {'arduino_core': core, 'target': target, 'profile': profile(core)}
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise ValueError(f'{archive}: mismatched {key}; rebuild this target')
    if manifest.get('sha256') != hashlib.sha256(archive.read_bytes()).hexdigest():
        raise ValueError(f'{archive}: checksum mismatch; rebuild this target')
    return archive
