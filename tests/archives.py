"""Check archive byte order, private crypto isolation, and public peer symbols."""
from pathlib import Path
import hashlib
import os
import subprocess
import argparse
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
from build_config import add_core_argument, profile, data_dir, verify_archive
parser = argparse.ArgumentParser(description=__doc__)
add_core_argument(parser)
args = parser.parse_args()
selected = profile(args.core_version)
nm = data_dir() / f"packages/esp32/tools/esp-x32/{selected['toolchain']}/bin/xtensa-esp-elf-nm.exe"
for target in ['esp32', 'esp32s3']:
    archive = verify_archive(args.core_version, target)
    data = archive.read_bytes()
    assert data[:8] == b'!<arch>\n'
    pos = 8
    objects = 0
    while pos < len(data):
        header = data[pos:pos+60]
        size = int(header[48:58].strip())
        member = data[pos+60:pos+60+size]
        if member.startswith(b'\x7fELF'):
            assert member[:6] == b'\x7fELF\x01\x01', (target, header[:16])
            objects += 1
        pos += 60 + size + (size % 2)
    result = subprocess.check_output([str(nm), '-g', str(archive)], text=True)
    definitions = set()
    for line in result.splitlines():
        fields = line.split()
        if len(fields) == 3:
            definitions.add(fields[2])
        if len(fields) in (2, 3) and fields[-1].startswith(('mbedtls_', 'psa_', 'srtp_')):
            raise AssertionError(f'{target}: unnamespaced private symbol: {line}')
    for name in ['esp_peer_open', 'esp_peer_get_default_impl', 'esp_peer_send_data',
                 'sinric_private_mbedtls_ssl_conf_dtls_srtp_protection_profiles',
                 'sinric_private_srtp_protect']:
        assert name in definitions, (target, name)
    print(f'PASS {target}: {objects} little-endian objects, crypto isolation, public API; SHA256 {hashlib.sha256(data).hexdigest()}')
