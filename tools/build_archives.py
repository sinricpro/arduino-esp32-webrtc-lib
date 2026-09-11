"""Build the private WebRTC archive using the installed Arduino ESP32 SDK (Windows).

No Arduino core files are modified. Run with Python 3.12 or newer.
"""
from pathlib import Path
import concurrent.futures
import os
import shlex
import subprocess
import argparse
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]

def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--target', default='esp32s3', choices=['esp32s3', 'esp32'])
    args = parser.parse_args()
    packages = Path(os.environ['LOCALAPPDATA']) / 'Arduino15/packages/esp32'
    sdk = packages / f'tools/{args.target}-libs/3.3.11'
    tool = packages / 'tools/esp-x32/2601/bin'
    gcc = tool / f'xtensa-{args.target}-elf-gcc.exe'
    ar = tool / 'xtensa-esp-elf-ar.exe'
    nm = tool / 'xtensa-esp-elf-nm.exe'
    objcopy = tool / 'xtensa-esp-elf-objcopy.exe'
    out = ROOT / f'build/{args.target}'
    out.mkdir(parents=True, exist_ok=True)
    mbed = ROOT / 'build-support/esp-idf/components/mbedtls/mbedtls'
    srtp = ROOT / 'build-support/esp-adf-libs/esp_libsrtp/libsrtp'
    peer = ROOT / 'upstream/components/esp_peer'
    if 'esp-idf: v5.5.5 b774170ff46' not in (sdk / 'versions.txt').read_text():
        raise SystemExit('Unexpected Arduino IDF SDK revision; refusing an ABI-mismatched build.')
    config = (mbed / 'include/mbedtls/mbedtls_config.h').read_text()
    disabled = ['MBEDTLS_NET_C', 'MBEDTLS_FS_IO', 'MBEDTLS_ENTROPY_NV_SEED',
                'MBEDTLS_PSA_CRYPTO_STORAGE_C', 'MBEDTLS_PSA_ITS_FILE_C',
                'MBEDTLS_SSL_PROTO_TLS1_3', 'MBEDTLS_SSL_SESSION_TICKETS',
                'MBEDTLS_SSL_TICKET_C']
    for name in disabled:
        config = config.replace('#define ' + name + '\n', '//#define ' + name + '\n')
    config += '\n#define MBEDTLS_SSL_DTLS_SRTP\n#define MBEDTLS_NO_PLATFORM_ENTROPY\n#define MBEDTLS_ENTROPY_HARDWARE_ALT\n#define MBEDTLS_PLATFORM_MS_TIME_ALT\n'
    (out / 'webrtc_mbedtls_config.h').write_text(config)
    (out / 'config.h').write_text('''#pragma once
#define PACKAGE_VERSION "3.0.0-dev"
#define PACKAGE_STRING "libsrtp 3.0.0-dev"
#define HAVE_ARPA_INET_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDLIB_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_UNISTD_H 1
#define HAVE_UINT8_T 1
#define HAVE_UINT16_T 1
#define HAVE_UINT32_T 1
#define HAVE_UINT64_T 1
#define HAVE_INT32_T 1
#define HAVE_INLINE 1
#define CPU_RISC 1
#define SIZEOF_UNSIGNED_LONG 4
#define SIZEOF_UNSIGNED_LONG_LONG 8
''')
    (out / 'esp_port.c').write_text('''#include <stddef.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/timing.h"
mbedtls_ms_time_t mbedtls_ms_time(void) { return esp_timer_get_time() / 1000; }
unsigned long mbedtls_timing_get_timer(struct mbedtls_timing_hr_time *t, int reset) {
    uint64_t now = esp_timer_get_time() / 1000;
    if (reset) { t->MBEDTLS_PRIVATE(opaque)[0] = now; return 0; }
    return now - t->MBEDTLS_PRIVATE(opaque)[0];
}
void mbedtls_timing_set_delay(void *p, uint32_t a, uint32_t b) {
    mbedtls_timing_delay_context *t = p;
    t->MBEDTLS_PRIVATE(int_ms) = a; t->MBEDTLS_PRIVATE(fin_ms) = b;
    mbedtls_timing_get_timer(&t->MBEDTLS_PRIVATE(timer), 1);
}
int mbedtls_timing_get_delay(void *p) {
    mbedtls_timing_delay_context *t = p;
    if (!t->MBEDTLS_PRIVATE(fin_ms)) return -1;
    unsigned long elapsed = mbedtls_timing_get_timer(&t->MBEDTLS_PRIVATE(timer), 0);
    return elapsed >= t->MBEDTLS_PRIVATE(fin_ms) ? 2 : elapsed >= t->MBEDTLS_PRIVATE(int_ms) ? 1 : 0;
}
uint32_t mbedtls_timing_get_final_delay(const mbedtls_timing_delay_context *t) {
    return t->MBEDTLS_PRIVATE(fin_ms);
}
int mbedtls_hardware_poll(void *ctx, unsigned char *out, size_t len, size_t *olen) {
    (void)ctx; esp_fill_random(out, len); *olen = len; return 0;
}
''')
    flags = shlex.split((sdk / 'flags/c_flags').read_text())
    flags += ['-Os', '-DESP_PLATFORM', '-D_GNU_SOURCE', '-DHAVE_CONFIG_H',
              '-DMBEDTLS_CONFIG_FILE="webrtc_mbedtls_config.h"',
              '-I' + str(out), '-I' + str(mbed / 'include'), '-I' + str(mbed / 'library'),
              '-I' + str(srtp / 'include'), '-I' + str(srtp / 'crypto/include'),
              '-I' + str(peer / 'include'), '-I' + str(peer / 'src'),
              '-I' + str(sdk / 'qio_qspi/include'),
              '-iprefix', str(sdk / 'include') + '/', '@' + str(sdk / 'flags/includes')]
    crypto_sources = [p for p in (mbed / 'library').glob('*.c') if p.name != 'timing.c'] + [out / 'esp_port.c']
    srtp_sources = ['srtp/srtp.c', 'crypto/cipher/cipher.c', 'crypto/cipher/cipher_test_cases.c',
                    'crypto/cipher/null_cipher.c', 'crypto/cipher/aes.c', 'crypto/cipher/aes_icm.c',
                    'crypto/hash/auth.c', 'crypto/hash/auth_test_cases.c', 'crypto/hash/null_auth.c',
                    'crypto/hash/hmac.c', 'crypto/hash/sha1.c', 'crypto/kernel/alloc.c',
                    'crypto/kernel/crypto_kernel.c', 'crypto/kernel/err.c', 'crypto/kernel/key.c',
                    'crypto/math/datatypes.c', 'crypto/replay/rdb.c', 'crypto/replay/rdbx.c']
    peer_sources = [peer / 'src' / f for f in ['esp_peer.c', 'media_lib_weak.c', 'dtls_srtp.c', 'peer_utils.c',
                    'transport/udp.c', 'transport/tcp.c', 'transport/tls.c', 'transport/peer_tls_esp.c']]
    def compile_one(item):
        category, src = item
        obj = out / (category + '_' + src.stem + '.o')
        result = subprocess.run([str(gcc), *flags, '-c', str(src), '-o', str(obj)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(str(src) + '\n' + result.stdout + result.stderr)
        if obj.read_bytes()[:6] != b'\x7fELF\x01\x01':
            raise RuntimeError(f'{obj}: expected a 32-bit little-endian ESP object')
        return obj
    items = [('crypto', p) for p in crypto_sources] + [('srtp', srtp / p) for p in srtp_sources] + [('peer', p) for p in peer_sources]
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        objects = list(pool.map(compile_one, items))
    # Namespace every private crypto definition, including references in the peer
    # binary. Arduino HTTPS keeps using its original, ABI-compatible Mbed TLS.
    symbols = set()
    for obj in objects:
        if obj.name.startswith(('crypto_', 'srtp_')):
            result = run([nm, '-g', '--defined-only', obj], capture_output=True, text=True)
            for line in result.stdout.splitlines():
                fields = line.split()
                if len(fields) == 3:
                    symbols.add(fields[2])
    mapping = out / 'symbols.map'
    mapping.write_text(''.join(f'{s} sinric_private_{s}\n' for s in sorted(symbols)))
    prebuilt = out / 'peer_default.a'
    run([objcopy, '--redefine-syms=' + str(mapping), peer / f'libs/{args.target}/libpeer_default.a', prebuilt])
    for obj in objects:
        run([objcopy, '--redefine-syms=' + str(mapping), obj])
    dest = ROOT / f'src/{args.target}'
    dest.mkdir(parents=True, exist_ok=True)
    archive = dest / 'libsinric_webrtc.a'
    # MRI ADDLIB preserves all upstream archive members.
    script = 'CREATE ' + archive.as_posix() + '\nADDLIB ' + prebuilt.as_posix() + '\n'
    script += ''.join('ADDMOD ' + p.as_posix() + '\n' for p in objects)
    script += 'SAVE\nEND\n'
    run([ar, '-M'], input=script, text=True)
    undefined = run([nm, '-u', archive], capture_output=True, text=True).stdout
    leaked = [line for line in undefined.splitlines() if len(line.split()) == 2 and line.split()[-1].startswith(('mbedtls_', 'psa_', 'srtp_'))]
    if leaked:
        raise RuntimeError('Unnamespaced private crypto references: ' + '\n'.join(leaked))
    manifest = {
        'target': args.target, 'arduino_core': '3.3.11', 'idf': '5.5.5',
        'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
        'private_symbols': len(symbols),
        'sources': {str(p.relative_to(ROOT)): run(['git', '-C', p, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
                    for p in [peer.parents[1], mbed, srtp.parents[1]]},
    }
    (dest / 'build-info.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Built {archive} ({archive.stat().st_size} bytes)')

if __name__ == '__main__':
    main()
