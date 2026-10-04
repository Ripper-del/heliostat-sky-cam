#!/usr/bin/env python3
"""Читає UART-лог плати N секунд і друкує його (для автоматизації без PuTTY).

Приклади:
  python scripts/readlog.py --list                 # показати доступні порти
  python scripts/readlog.py COM5 --seconds 8       # Windows
  python scripts/readlog.py /dev/ttyACM0 -s 8      # Linux
  python scripts/readlog.py /dev/tty.usbmodem1303 -s 8   # macOS
  python scripts/readlog.py COM5 -s 8 --reset      # перезапустити плату перед читанням

Залежність: pip install pyserial
"""
import argparse, sys, time
try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("Потрібен pyserial: pip install pyserial")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?")
    ap.add_argument("-s", "--seconds", type=float, default=6.0)
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--reset", action="store_true", help="смикнути DTR/RTS (для ST-Link зазвичай не працює; використайте кнопку RESET)")
    ap.add_argument("-o", "--out", help="зберегти лог у файл")
    a = ap.parse_args()

    if a.list or not a.port:
        for p in list_ports.comports():
            print(f"{p.device}\t{p.description}")
        return
    lines = []
    with serial.Serial(a.port, a.baud, timeout=0.2) as s:
        t0 = time.time()
        while time.time() - t0 < a.seconds:
            chunk = s.read(512)
            if chunk:
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text); sys.stdout.flush()
                lines.append(text)
    if a.out:
        open(a.out, "w", encoding="utf-8").write("".join(lines))

if __name__ == "__main__":
    main()
