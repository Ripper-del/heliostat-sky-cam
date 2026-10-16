#!/usr/bin/env python3
"""Надіслати однолітерну команду CLI прошивці по UART (щоб запустити транзакцію, яку пише аналізатор).

  python3 scripts/sendcmd.py m                  # MPU: WHO_AM_I + burst
  python3 scripts/sendcmd.py c --wait 1.0       # камера PID/VER, 1 с чекати і друкувати відповідь
  python3 scripts/sendcmd.py f --repeat 3 --pause 2   # JEDEC тричі з паузою 2 с
Порт: перший /dev/cu.usbmodem* (або --port). Потрібен pyserial:  pip3 install pyserial
Команди: див. README.md (розділ CLI). Монітор (picocom) під час запису закрити — порт зайнятий.
"""
import argparse, glob, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("cmd", help="символ команди: s i m c f w p a e 1 2 k x")
ap.add_argument("--port")
ap.add_argument("--wait", type=float, default=1.5, help="скільки секунд читати відповідь")
ap.add_argument("--repeat", type=int, default=1)
ap.add_argument("--pause", type=float, default=1.0)
a = ap.parse_args()
try:
    import serial
except ImportError:
    sys.exit("pip3 install pyserial")
port = a.port or next(iter(sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))), None)
if not port:
    sys.exit("UART-порт не знайдено (плата підключена? --port /dev/cu.usbmodemXXXX)")
s = serial.Serial(port, 115200, timeout=0.2)
for i in range(a.repeat):
    s.reset_input_buffer()
    s.write(a.cmd[0].encode())
    t = time.time() + a.wait
    out = b""
    while time.time() < t:
        out += s.read(4096)
    print(out.decode(errors="replace").strip() or "(немає відповіді)")
    if i + 1 < a.repeat:
        time.sleep(a.pause)
