"""Hardware check for a SinricProWebRTC camera: build, flash, capture serial and judge the stream.

A run passes only when the radio stayed healthy, not merely when frames arrived. Wi-Fi TX
exhaustion shows up as `sendto deferred ... errno 12` and as mbedtls WANT_WRITE (-26752) while the
heap still looks fine. A faster sender can raise the frame rate and still fail here.

Build the sketch with -DSINRICPRO_WEBRTC_DIAG so the session prints its DIAG line every 2 s. The
example sketches' 30 s heap line (Heap free ... RSSI ..., streaming: ...) is used when present.

    # Build, flash, then capture while you open Preview in the portal or app:
    python tools/hw_test.py --sketch examples/SinricProCamera --fqbn esp32:esp32:esp32:PSRAM=enabled,PartitionScheme=huge_app \\
        --port COM6 --define CAMERA_BOARD=BOARD_AI_THINKER --seconds 180

    # Judge a capture you already have:
    python tools/hw_test.py --log build/serial.log

Exit status: 0 pass, 1 fail, 2 inconclusive (no streaming seen).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

DIAG = re.compile(
    r'DIAG (\d+)s: done (\d+) drop (\d+) \| avg frame (\d+) B, avg send (\d+) ms, blocked (\d+) \| '
    r'interval (\d+) ms q(-?\d+) \| loop iters (\d+), main_loop avg (\d+) us max (\d+) us(?: \| codec (\w+))?')
DEFERRED = re.compile(r'sendto deferred \((\d+) so far\): errno \d+ \([^)]*\), (\d+) bytes')
HEAP = re.compile(r'Heap free (\d+) \(min (\d+)\), internal (\d+) \(largest (\d+)\), PSRAM free (\d+), '
                  r'RSSI (-?\d+), streaming: (yes|no)')


@dataclass
class Metrics:
    diag_lines: int = 0
    streaming_seconds: int = 0
    frames: int = 0
    dropped: int = 0
    fps: float = 0.0
    codecs: list = field(default_factory=list)
    jpeg_quality: list = field(default_factory=list)
    main_loop_avg_us: int = 0
    main_loop_max_us: int = 0
    deferred_sends: int = 0
    largest_deferred_bytes: int = 0
    mbedtls_want_write: int = 0
    offers: int = 0
    answers: int = 0
    answer_failures: int = 0
    dtls_closes: int = 0
    rssi_min: int = None
    rssi_max: int = None
    internal_heap_first: int = None
    internal_heap_last: int = None
    internal_largest_min: int = None


def parse_log(text):
    m = Metrics()
    diag = [tuple(x) for x in DIAG.findall(text)]
    m.diag_lines = len(diag)
    if diag:
        # The first line of a session covers ICE and DTLS setup rather than streaming.
        steady = diag[1:] if len(diag) > 1 else diag
        m.frames = sum(int(d[1]) for d in steady)
        m.dropped = sum(int(d[2]) for d in diag)
        first, last = int(steady[0][0]), int(steady[-1][0])
        m.streaming_seconds = last - first + 2
        m.fps = round(m.frames / m.streaming_seconds, 2)
        m.codecs = sorted({d[11] or 'jpeg' for d in diag})
        m.jpeg_quality = sorted({int(d[7]) for d in diag})
        iterations = sum(int(d[8]) for d in diag)
        m.main_loop_avg_us = sum(int(d[8]) * int(d[9]) for d in diag) // iterations if iterations else 0
        m.main_loop_max_us = max(int(d[10]) for d in diag)

    for count, size in DEFERRED.findall(text):
        m.deferred_sends = max(m.deferred_sends, int(count))
        m.largest_deferred_bytes = max(m.largest_deferred_bytes, int(size))
    m.mbedtls_want_write = text.count('mbedtls_ssl_read error: -26752')
    m.offers = text.count('WebRTC offer for ')
    m.answers = text.count('WebRTC answer sent')
    m.answer_failures = text.count('WebRTC answer failed')
    m.dtls_closes = text.count('Detected DTLS connection close')

    heap = HEAP.findall(text)
    if heap:
        rssi = [int(h[5]) for h in heap]
        m.rssi_min, m.rssi_max = min(rssi), max(rssi)
        streaming = [h for h in heap if h[6] == 'yes']
        if streaming:
            m.internal_heap_first = int(streaming[0][2])
            m.internal_heap_last = int(streaming[-1][2])
            m.internal_largest_min = min(int(h[3]) for h in streaming)
    return m


@dataclass
class Limits:
    min_streaming_seconds: int = 20
    min_fps: float = 0.0
    max_dropped: int = 0
    # One small deferral during ICE setup appears on healthy links as well.
    max_deferred_sends: int = 1
    max_mbedtls_want_write: int = 0
    max_heap_drop: int = 4096
    weak_rssi: int = -75


def evaluate(m, limits):
    """Returns (verdict, reasons); verdict is 'PASS', 'FAIL' or 'INCONCLUSIVE'."""
    failures, notes = [], []
    if m.deferred_sends > limits.max_deferred_sends:
        failures.append(f'{m.deferred_sends} deferred sends, largest {m.largest_deferred_bytes} B: Wi-Fi TX buffers exhausted')
    if m.mbedtls_want_write > limits.max_mbedtls_want_write:
        failures.append(f'{m.mbedtls_want_write} mbedtls WANT_WRITE errors: DTLS could not send')
    if m.dropped > limits.max_dropped:
        failures.append(f'{m.dropped} frames dropped')
    if m.answer_failures:
        failures.append(f'{m.answer_failures} offers not answered')
    if m.internal_heap_first is not None and m.internal_heap_first - m.internal_heap_last > limits.max_heap_drop:
        failures.append(f'internal heap fell {m.internal_heap_first - m.internal_heap_last} B while streaming')
    if m.diag_lines and limits.min_fps and m.fps < limits.min_fps:
        failures.append(f'{m.fps} fps is below {limits.min_fps}')
    if m.rssi_min is not None and m.rssi_min < limits.weak_rssi:
        notes.append(f'RSSI reached {m.rssi_min} dBm, below {limits.weak_rssi}: failures may be the link, not the code')
    if not m.diag_lines:
        notes.append('no DIAG lines: no viewer streamed during the capture, or the firmware lacks -DSINRICPRO_WEBRTC_DIAG')

    if failures:
        return 'FAIL', failures + notes
    if m.streaming_seconds < limits.min_streaming_seconds:
        return 'INCONCLUSIVE', [f'only {m.streaming_seconds} s of streaming, need {limits.min_streaming_seconds}'] + notes
    return 'PASS', notes


def summary(m):
    rssi = f'{m.rssi_min}..{m.rssi_max} dBm' if m.rssi_min is not None else 'n/a'
    heap = (f'{m.internal_heap_first}->{m.internal_heap_last} B (largest min {m.internal_largest_min})'
            if m.internal_heap_first is not None else 'n/a')
    return (f'{m.fps} fps over {m.streaming_seconds} s ({m.frames} frames, {m.dropped} dropped, '
            f'codec {",".join(m.codecs) or "n/a"}, q{m.jpeg_quality}) | main_loop avg {m.main_loop_avg_us} us '
            f'max {m.main_loop_max_us} us | deferred {m.deferred_sends} | mbedtls {m.mbedtls_want_write} | '
            f'offers {m.offers}, answered {m.answers}, DTLS closes {m.dtls_closes} | RSSI {rssi} | heap {heap}')


def arduino_cli(explicit):
    cli = explicit or os.environ.get('ARDUINO_CLI') or shutil.which('arduino-cli')
    if not cli:
        sys.exit('arduino-cli not found: pass --cli or set ARDUINO_CLI')
    return cli


def build_and_flash(args, build_dir):
    cli = arduino_cli(args.cli)
    flags = ' '.join(['-DSINRICPRO_WEBRTC_DIAG'] + [f'-D{d}' for d in args.define])
    command = [cli, 'compile', '-b', args.fqbn, '--build-path', str(build_dir),
               '--build-property', f'compiler.cpp.extra_flags={flags}']
    for library in args.library:
        command += ['--library', library]
    subprocess.run(command + [args.sketch], check=True)
    if not args.no_flash:
        subprocess.run([cli, 'upload', '-p', args.port, '-b', args.fqbn, '--input-dir', str(build_dir)], check=True)


def capture(port_name, seconds, reset, output):
    import serial  # only needed when talking to a board

    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    connection.dtr = False
    connection.rts = False
    connection.port = port_name
    connection.open()
    interesting = re.compile(r'DIAG|WebRTC|deferred|mbedtls|DTLS|Heap free|Connected to SinricPro')
    chunks, pending = [], ''
    with connection as port:
        if reset:
            port.rts = True
            time.sleep(0.15)
            port.rts = False
        print(f'Capturing {seconds} s. Open Preview once the board reports "Connected to SinricPro".', flush=True)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            data = port.read(port.in_waiting or 1)
            if not data:
                continue
            chunks.append(data)
            pending += data.decode('utf-8', errors='replace')
            *lines, pending = pending.split('\n')
            for line in lines:
                if interesting.search(line):
                    print(line.rstrip(), flush=True)
    text = b''.join(chunks).decode('utf-8', errors='replace')
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text(text, encoding='utf-8')
    return text


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--log', help='judge an existing capture instead of building and flashing')
    p.add_argument('--sketch')
    p.add_argument('--fqbn')
    p.add_argument('--port')
    p.add_argument('--define', action='append', default=[], help='extra -D define, e.g. CAMERA_BOARD=BOARD_AI_THINKER')
    p.add_argument('--library', action='append', default=[], help='library path to prefer over installed ones')
    p.add_argument('--cli', help='arduino-cli path (default: $ARDUINO_CLI or PATH)')
    p.add_argument('--no-flash', action='store_true', help='capture from the firmware already on the board')
    p.add_argument('--no-reset', action='store_true', help='do not reset the board before capturing')
    p.add_argument('--seconds', type=int, default=180)
    p.add_argument('--output', default='build/hw_test/serial.log')
    p.add_argument('--json', action='store_true', help='print metrics and verdict as JSON')
    p.add_argument('--min-fps', type=float, default=Limits.min_fps)
    p.add_argument('--min-streaming-seconds', type=int, default=Limits.min_streaming_seconds)
    p.add_argument('--max-dropped', type=int, default=Limits.max_dropped)
    args = p.parse_args(argv)

    if args.log:
        text = Path(args.log).read_text(encoding='utf-8', errors='replace')
    else:
        if not args.port or (not args.no_flash and not (args.sketch and args.fqbn)):
            p.error('--port is required, and --sketch and --fqbn unless --no-flash')
        if not args.no_flash:
            try:
                build_and_flash(args, Path(args.output).parent / 'build')
            except subprocess.CalledProcessError as error:
                # arduino-cli has already printed why; a native-USB board that dropped off the bus
                # needs replugging, or BOOT held while plugging in.
                step = 'upload' if error.cmd[1] == 'upload' else 'build'
                print(f'FAIL\n  - {step} failed (arduino-cli exit {error.returncode})')
                return 1
        text = capture(args.port, args.seconds, not args.no_reset, args.output)

    metrics = parse_log(text)
    limits = Limits(min_streaming_seconds=args.min_streaming_seconds, min_fps=args.min_fps,
                    max_dropped=args.max_dropped)
    verdict, reasons = evaluate(metrics, limits)
    if args.json:
        print(json.dumps({'verdict': verdict, 'reasons': reasons, 'metrics': asdict(metrics)}, indent=2))
    else:
        print(summary(metrics))
        print(verdict + ''.join(f'\n  - {r}' for r in reasons))
    return {'PASS': 0, 'FAIL': 1, 'INCONCLUSIVE': 2}[verdict]


if __name__ == '__main__':
    sys.exit(main())
