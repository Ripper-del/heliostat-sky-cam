#!/usr/bin/env python3
"""Розбір текстових анотацій sigrok (docs/captures/*.txt) та автоматична перевірка записів.

  python3 scripts/check_captures.py                       # перевірити всі docs/captures/*.txt
  python3 scripts/check_captures.py --dir DIR             # інша тека (напр. scripts/fixtures)
  python3 scripts/check_captures.py --vectors i2c_mpu6050_whoami_burst.txt
        # надрукувати C-масив 6 байтів ACCEL з I²C-запису для tests/vectors/vectors.h

Очікування за префіксом імені файлу (що саме має бути видно на записі):
  i2c_nack_*  аномалія: звернення до 0x69 завершується NACK (пристрою немає)
  i2c_*   адреса 0x68 write, регістр 0x75 (WHO_AM_I), читання 0x68; burst-читання 6 байтів після регістра 0x3B
  sccb_*  адреса 0x42 write, регістр 0x0A, адреса 0x43 read, дані 0x76
  spi_*   MOSI 9F, MISO EF 40 17 (JEDEC); 06 (WE) + 20/02 7FF000 (стирання/запис)
  pwm_*   скважність ≈ 5 % / 7,5 % / 10 % (імпульси 1000 / 1500 / 2000 мкс), період ≈ 20 мс
  adc_*   (.sr напряму) імпульси маркера PA5 на D1: ширина ≈ 100–250 мкс, ≥ 3 імпульси
  uart_*  байти RX складаються в ASCII-текст прошивки: є рядок "tick" та "uptime"
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
        if name.startswith("i2c_nack"):
            n = [x for x in t if x["rw"] == "W" and x["addr"] == 0x69 and "NACK" in x["acks"]]
            if len(n) < 3: problems.append(f"очікується ≥ 3 звернень до 0x69 з NACK (знайдено {len(n)})")
            problems.append("УВАГА (для звіту, п. 4.4): аномалія NACK на адресі 0x69 — навмисна (відсутній пристрій)")
        elif name.startswith("i2c_"):
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
    elif name.startswith("uart_"):
        data = bytes(int(m.group(1), 16) for m in re.finditer(r'^uart-\d+: ([0-9A-Fa-f]{2})$', text, re.M))
        txt = data.decode("ascii", errors="replace")
        if "tick" not in txt: problems.append('у декодованому тексті немає рядка "tick"')
        if "uptime" not in txt: problems.append('у декодованому тексті немає "uptime"')
    elif name.startswith("pwm_"):
        duty = [float(m.group(1)) for m in re.finditer(r'^pwm-\d+: ([0-9.]+)%$', text, re.M)]
        per = [float(m.group(1)) for m in re.finditer(r'^pwm-\d+: ([0-9.]+) ms$', text, re.M)]
        for lo, hi, label in ((4.9, 5.1, "1000 мкс"), (7.4, 7.6, "1500 мкс"), (9.9, 10.1, "2000 мкс")):
            if not any(lo <= d <= hi for d in duty): problems.append(f"немає імпульсів ≈ {label} (скважність {lo}–{hi} %)")
        if not any(19.0 <= x <= 20.5 for x in per): problems.append("немає періоду ≈ 20 мс (19–20,5 мс)")
    elif name.startswith("spi_"):
        mosi, miso = spi_bytes(text)
        def has(seq, sub): return any(seq[i:i + len(sub)] == sub for i in range(len(seq) - len(sub) + 1))
        if not (has(mosi, [0x9F, 0, 0, 0]) or has(mosi, [0x9F, 0xFF, 0xFF, 0xFF])): problems.append("немає команди 0x9F + 3 dummy-байтів (00 або FF) на MOSI")
        if not has(miso, [0xEF, 0x40, 0x17]): problems.append(f"MISO не містить EF 40 17 (є: {' '.join(f'{b:02X}' for b in miso[:8])})")
        if 0x06 not in mosi: problems.append("немає команди 0x06 (Write Enable) перед записом")
        if not has(mosi, [0x20, 0x7F, 0xF0, 0x00]): problems.append("немає 0x20 + адреса 7FF000 (Sector Erase)")
        if not has(mosi, [0x02, 0x7F, 0xF0, 0x00]): problems.append("немає 0x02 + адреса 7FF000 (Page Program)")
    else:
        return None
    return problems


def marker_pulses(path):
    """Читає .sr (zip з logic-1-N) і повертає [(старт_мкс, ширина_мкс)] коротких імпульсів на D1 (< 2 мс); LED PA5 мигає ~100 мс."""
    import zipfile
    z = zipfile.ZipFile(path)
    chunks = sorted((n for n in z.namelist() if n.startswith("logic-1")),
                    key=lambda n: [int(t) if t.isdigit() else 0 for t in n.split("-")])
    data = b"".join(z.read(n) for n in chunks)
    out, prev, start = [], (data[0] >> 1) & 1 if data else 0, 0
    for i, b in enumerate(data):
        v = (b >> 1) & 1
        if v != prev:
            if v == 1: start = i
            elif i - start < 2000: out.append((start, i - start))
            prev = v
    return out


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
    for f in sorted(pathlib.Path(a.dir).glob("adc_*.sr")):
        m = marker_pulses(f)
        hard = [] if len(m) >= 3 and all(100 <= w <= 250 for _, w in m) else [f"маркер PA5: знайдено {len(m)} імпульсів, ширини {[w for _, w in m][:5]} мкс (очікується ≥ 3, 100–250 мкс)"]
        print(f"[{'FAIL' if hard else ' ok '}] {f.name}")
        for x in hard: print("       -", x)
        if not hard: print(f"       - імпульсів маркера: {len(m)}, ширина {min(w for _, w in m)}–{max(w for _, w in m)} мкс")
        bad += bool(hard)
    for f in files:
        p = check(f)
        if p is None: continue
        hard = [x for x in p if not x.startswith("УВАГА")]
        print(f"[{'FAIL' if hard else ' ok '}] {f.name}")
        for x in p: print("       -", x)
        bad += bool(hard)
    sys.exit(1 if bad else 0)
