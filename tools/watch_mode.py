#!/usr/bin/env python3
"""Пише режим, суму світла і класифікацію неба раз на ~0,8 с (для перевірок FR вручну). Використання: watch_mode.py <секунд>"""
import glob
import re
import sys
import time

import serial

s = serial.Serial(sorted(glob.glob("/dev/cu.usbmodem*"))[0], 115200, timeout=0.05)
t0, buf, nxt, sky = time.time(), b"", 0, "?"
while time.time() - t0 < float(sys.argv[1] if len(sys.argv) > 1 else 25):
    if time.time() >= nxt:
        s.write(b"\rMODE\r"); nxt = time.time() + 0.8
    d = s.read(4096)
    if d:
        buf += d
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            l = line.decode(errors="replace")
            m2 = re.search(r"sky=(\w+)", l)
            if m2: sky = m2.group(1)
            m = re.search(r"mode=(\w+) light=(\d+) .*ex=(-?\d+) ey=(-?\d+) az=(\d+) el=(\d+) servos=(\w+)", l)
            if m: print("%5.1f %-8s light=%5s ex=%4s ey=%4s az=%s el=%s servos=%s sky=%s" % ((time.time() - t0, m.group(1), m.group(2), m.group(3), m.group(4), m.group(5), m.group(6), m.group(7), sky)), flush=True)
