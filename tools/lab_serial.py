"""Спільна бібліотека для хостових скриптів ЛР3: UART до прошивки, команди з повтором, розбір STAT/LOAD/MEM.

Важливо (методичка, п. 5.2): час прибуття рядка на хості НЕ використовується для вимірювання затримок —
затримки міряє сама прошивка (DWT) і друкує їх у STAT. Хостові секундоміри тут лише для керування сценарієм
і для оцінки «скільки команд лишилось без відповіді».
"""
import glob
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pip3 install pyserial   (або ~/heliostat-venv/bin/python)")

STAT_RE = re.compile(r"^STAT name=(\S+) (.*)$")
KV_RE = re.compile(r"(\w+)=(\S+)")


def kv(text):
    """'a=1 b=2' -> {'a': 1, 'b': 2} (числа перетворюються на int)."""
    out = {}
    for k, v in KV_RE.findall(text):
        out[k] = int(v) if re.fullmatch(r"-?\d+", v) else v
    return out


def parse_stat_lines(lines):
    """Список рядків -> {name: {...}} для рядків 'STAT name=...' (останній запис кожного імені)."""
    out = {}
    for ln in lines:
        m = STAT_RE.match(ln.strip())
        if m:
            out[m.group(1)] = kv(m.group(2))
    return out


def parse_load(lines):
    for ln in lines:
        if ln.startswith("LOAD "):
            return kv(ln[5:])
    return {}


def parse_mem(lines):
    for ln in lines:
        if ln.startswith("MEM "):
            return kv(ln[4:])
    return {}


class Board:
    def __init__(self, port=None, baud=115200, log=None):
        port = port or next(iter(sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))), None)
        if not port:
            sys.exit("UART-порт не знайдено (плата підключена? --port ...)")
        self.s = serial.Serial(port, baud, timeout=0.05)
        self.log = log
        self.buf = b""

    def _emit(self, line):
        if self.log:
            self.log.write(line + "\n")
            self.log.flush()

    def read_lines(self, seconds):
        """Читає все, що приходить протягом seconds; повертає повні рядки."""
        end = time.time() + seconds
        lines = []
        while time.time() < end:
            d = self.s.read(4096)
            if d:
                self.buf += d
                *done, self.buf = self.buf.split(b"\n")
                for raw in done:
                    ln = raw.decode(errors="replace").strip("\r")
                    if ln:
                        lines.append(ln)
                        self._emit(ln)
        return lines

    def send(self, cmd, wait=2.0, retries=4):
        """Надсилає команду з Enter; прошивка під час довгої операції може втратити байти (це ЛР3-явище),
        тому повторюємо, доки не прийде відповідь, відмінна від 'tick'. Повертає (рядки, спроби)."""
        for attempt in range(1, retries + 1):
            self.s.write(b"\r")
            time.sleep(0.03)
            self.s.write(cmd.encode() + b"\r")
            lines = [l for l in self.read_lines(wait) if not l.startswith("tick") and "unknown command" not in l]
            if lines:
                return lines, attempt
        return [], retries

    def send_raw(self, data):
        self.s.write(data)

    def stats(self):
        lines, _ = self.send("STATS", wait=2.5)
        return parse_stat_lines(lines), parse_load(lines), lines

    def mem(self):
        lines, _ = self.send("MEM", wait=1.5)
        return parse_mem(lines)

    def version(self):
        lines, _ = self.send("VER", wait=1.5)
        return next((l for l in lines if l.startswith("VER ")), "VER unknown")
