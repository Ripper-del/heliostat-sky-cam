#!/usr/bin/env python3
"""Для сценарію «зникнення живлення під час запису у flash»: кожні ~3,2 с надсилає SNAP і переживає відключення плати.
  python3 tools/snap_loop.py 90      # 90 с; у цей час вдруге-втретє висмикуйте USB плати і вставляйте назад
"""
import glob
import sys
import time

import serial

end = time.time() + float(sys.argv[1] if len(sys.argv) > 1 else 90)
s = None
sent = 0
while time.time() < end:
    try:
        if s is None:
            port = next(iter(sorted(glob.glob("/dev/cu.usbmodem*"))), None)
            if port:
                s = serial.Serial(port, 115200, timeout=0.1)
        if s:
            s.write(b"\rSNAP\r"); sent += 1
    except Exception:
        s = None
    time.sleep(3.2)
print("надіслано SNAP:", sent)
