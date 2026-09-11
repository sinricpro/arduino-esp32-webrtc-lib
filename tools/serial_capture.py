"""Capture serial boot diagnostics; optionally reset using DTR/RTS."""
import argparse
import time
from pathlib import Path
import serial

p = argparse.ArgumentParser()
p.add_argument('--port', default='COM6')
p.add_argument('--seconds', type=int, default=25)
p.add_argument('--reset', action='store_true')
p.add_argument('--output', default='build/serial.log')
args = p.parse_args()
connection = serial.Serial(port=None, baudrate=115200, timeout=0.2)
connection.dtr = False
connection.rts = False
connection.port = args.port
connection.open()
with connection as port:
    if args.reset:
        port.dtr = False
        port.rts = True
        time.sleep(0.15)
        port.rts = False
    deadline = time.monotonic() + args.seconds
    with Path(args.output).open('wb') as log:
        while time.monotonic() < deadline:
            data = port.read(port.in_waiting or 1)
            if data:
                log.write(data); log.flush()
                print(data.decode('utf-8', errors='replace'), end='', flush=True)
