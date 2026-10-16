#!/usr/bin/env python3
"""Розбір текстових анотацій sigrok (docs/captures/*.txt) та автоматична перевірка записів.

  python3 scripts/check_captures.py                       # перевірити всі docs/captures/*.txt
  python3 scripts/check_captures.py --dir DIR             # інша тека (напр. scripts/fixtures)
  python3 scripts/check_captures.py --vectors i2c_mpu6050_whoami_burst.txt
        # надрукувати C-масив 6 байтів ACCEL з I²C-запису для tests/vectors/vectors.h

Очікування за префіксом імені файлу (що саме має бути видно на записі):
  i2c_*   адреса 0x68 write, регістр 0x75 (WHO_AM_I), читання 0x68; burst-читання 6 байтів після регістра 0x3B
  sccb_*  адреса 0x42 write, регістр 0x0A, адреса 0x43 read, дані 0x76
  spi_*   MOSI 9F, MISO EF 40 17 (JEDEC)
"""
import argparse, pathlib, re, sys

LINE = re.compile(r'^\w+-\d+: (.*)$')


def transactions(text):
    """Повертає список транзакцій: dict(addr, rw, data=[...], acks=[...]) між Start/Stop."""
    txns, cur = [], None
    for raw in text.splitlines():
        m = LINE.match(raw)
        if not m:
            continue
        a = m.group(1)
        if a in ("Start", "Start repeat"):
            cur = {"addr": None, "rw": None, "data": [], "acks": []}
            txns.append(cur)
        elif cur is None:
            continue
        elif a.startswith("Address write:"):
            cur["addr"], cur["rw"] = int(a.split(":")[1], 16), "W"
        elif a.startswith("Address read:"):
            cur["addr"], cur["rw"] = int(a.split(":")[1], 16), "R"
        elif a.startswith("Data write:") or a.startswith("Data read:"):
            cur["data"].append(int(a.split(":")[1], 16))
        elif a in ("ACK", "NACK"):
            cur["acks"].append(a)
        elif a == "Stop":
            cur = None
    return txns


def spi_bytes(text):
    mosi, miso = [], []
    for raw in text.splitlines():            # формат, який пише decode_captures.sh: "MOSI 9F" / "MISO EF"
        p = raw.split()
        if len(p) == 2 and p[0] == "MOSI": mosi.append(int(p[1], 16))
        if len(p) == 2 and p[0] == "MISO": miso.append(int(p[1], 16))
    return mosi, miso


def check(path):
    name = path.stem
    text = path.read_text(encoding="utf-8")
    problems = []
    if name.startswith(("i2c_", "sccb_")):
        t = transactions(text)
        if name.startswith("i2c_"):
            w = [x for x in t if x["rw"] == "W" and x["addr"] == 0x68]
            r = [x for x in t if x["rw"] == "R" and x["addr"] == 0x68]
            if not any(x["data"][:1] == [0x75] for x in w): problems.append("немає запису регістра 0x75 (WHO_AM_I) на адресу 0x68")
            if not any(x["data"][:1] == [0x68] for x in r): problems.append("немає читання WHO_AM_I = 0x68")
            if not any(len(x["data"]) >= 6 for x in r): problems.append("немає багатобайтового читання (≥ 6 байтів)")
        else:
            w = [x for x in t if x["rw"] == "W" and x["addr"] == 0x21]   # sigrok показує 7-бітну адресу
            r = [x for x in t if x["rw"] == "R" and x["addr"] == 0x21]
            if not any(x["data"][:1] == [0x0A] for x in w): problems.append("немає запису регістра PID (0x0A) на адресу 0x21 (0x42)")
            if not any(x["data"][:1] == [0x76] for x in r): problems.append("немає читання PID = 0x76 (адреса 0x21 read = 0x43)")
        if any("NACK" in x["acks"] for x in t if x["rw"] == "W"):
            problems.append("УВАГА (для звіту, п. 4.4): у фазі запису є NACK — це і є аномалія SCCB/I²C")
    elif name.startswith("spi_"):
        mosi, miso = spi_bytes(text)
        if 0x9F not in mosi: problems.append("немає команди 0x9F на MOSI")
        if miso[-3:] != [0xEF, 0x40, 0x17] and [0xEF, 0x40, 0x17] != miso[1:4]:
            problems.append(f"MISO не містить EF 40 17 (є: {' '.join(f'{b:02X}' for b in miso[:8])})")
    else:
        return None
    return problems


def vectors(path):
    t = transactions(path.read_text(encoding="utf-8"))
    for i, x in enumerate(t):
        if x["rw"] == "W" and x["addr"] == 0x68 and x["data"][:1] == [0x3B]:
            for y in t[i:]:
                if y["rw"] == "R" and y["addr"] == 0x68 and len(y["data"]) >= 6:
                    b = y["data"][:6]
                    print("static const unsigned char VEC_MPU_ACCEL_BURST[6] = { " + ", ".join(f"0x{v:02X}" for v in b) + " };")
                    s16 = lambda h, l: ((h << 8) | l) - 65536 if (h << 8 | l) & 0x8000 else (h << 8) | l
                    print(f"/* ax={s16(b[0], b[1])} ay={s16(b[2], b[3])} az={s16(b[4], b[5])} (сирі значення) */")
                    return 0
    print("Не знайдено burst-читання 0x3B + ≥ 6 байтів на адресі 0x68", file=sys.stderr)
    return 1


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=str(pathlib.Path(__file__).resolve().parent.parent / "docs" / "captures"))
    ap.add_argument("--vectors", metavar="FILE.txt")
    a = ap.parse_args()
    if a.vectors:
        sys.exit(vectors(pathlib.Path(a.dir) / a.vectors))
    bad = 0
    files = sorted(pathlib.Path(a.dir).glob("*.txt"))
    if not files:
        print("Немає .txt (спершу: make decode)"); sys.exit(1)
    for f in files:
        p = check(f)
        if p is None: continue
        hard = [x for x in p if not x.startswith("УВАГА")]
        print(f"[{'FAIL' if hard else ' ok '}] {f.name}")
        for x in p: print("       -", x)
        bad += bool(hard)
    sys.exit(1 if bad else 0)
