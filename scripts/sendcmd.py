#!/usr/bin/env python3
"""Надіслати команду CLI прошивці по UART: один символ (старі команди) або рядок (STATS, CAL SHOW, LOG 5 ...; Enter додається).

  python3 scripts/sendcmd.py m                  # MPU: WHO_AM_I + burst
  python3 scripts/sendcmd.py c --wait 1.0       # камера PID/VER, 1 с чекати і друкувати відповідь
  python3 scripts/sendcmd.py f --repeat 3 --pause 2   # JEDEC тричі з паузою 2 с
  python3 scripts/sendcmd.py "STATS" --wait 2         # рядкова команда
Порт: перший /dev/cu.usbmodem* (або --port). Потрібен pyserial:  pip3 install pyserial
Команди: див. README.md (розділ CLI). Монітор (picocom) під час запису закрити — порт зайнятий.
"""
import argparse, glob, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("cmd", help="символ команди (s i m c f w p a e b g 1 2 k x r) або рядок (STATS, CAL SHOW, ...)")
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
s = serial.Serial(port, 115200, timeout=0.2, write_timeout=3)
for i in range(a.repeat):
    s.reset_input_buffer()
    try:
        s.write(a.cmd.encode() + (b"\r" if len(a.cmd) > 1 else b""))
    except serial.SerialTimeoutException:   # ST-Link перестав приймати дані від комп'ютера (docs/problems.md)
        sys.exit("ST-Link VCP не приймає дані (запис у порт заблоковано). Вимкніть USB плати на 15 с і підключіть знову.")
    t = time.time() + a.wait
    out = b""
    while time.time() < t:
        out += s.read(4096)
    if s.out_waiting:   # байти так і не пішли в плату: ST-Link перестав приймати дані від комп'ютера (docs/problems.md)
        print("ST-Link VCP не приймає дані (байти застрягли у вихідному буфері). Вимкніть USB плати на 15 с і підключіть знову.", flush=True)
        os._exit(3)     # не закривати порт: close() чекав би, поки буфер спорожніє, тобто вічно
    print(out.decode(errors="replace").strip() or "(немає відповіді)")
    if i + 1 < a.repeat:
        time.sleep(a.pause)
