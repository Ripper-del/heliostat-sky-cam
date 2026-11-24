#!/usr/bin/env python3
"""ЛР5: прогін сценаріїв п. 9.2–9.3 командою TEST AUTO. Прошивка сама вмикає сценарії, тримає навантаження
(BTNSIM 700 і знімок кожні 3,2 с) і після кожного друкує STATS і QSTAT; хост надсилає одну команду і далі лише читає.
Так прогін не залежить від зв'язку «комп'ютер -> плата», який на нашому стенді іноді зависає (docs/problems.md, проблема 36).

  python3 tools/lr5_auto.py [--prefix lr5]          # логи docs/metrics/<prefix>_auto_<сценарій>_<час>.log
"""
import argparse
import datetime
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab_serial import Board  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "metrics")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prefix", default="lr5")
    ap.add_argument("--timeout", type=int, default=420)
    ap.add_argument("--start", type=int, default=0, help="почати з n-го сценарію (TEST AUTO n)")
    a = ap.parse_args()
    b = Board()
    ver = b.version()
    print(ver, flush=True)
    b._write(b"\rTEST AUTO %d\r" % a.start)
    lines, cur, t0 = [], None, time.time()
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    while time.time() - t0 < a.timeout:
        for l in b.read_lines(1.0):
            lines.append(l)
            m = re.match(r"\[auto\] (begin|end) (\S+)", l)
            if m:
                print(l, flush=True)
        if any(l.startswith("[auto] all done") for l in lines[-50:]):
            break
    # розкласти вивід по сценаріях: від "[auto] begin X" до кінця блоку після "[auto] end X"
    blocks, name, buf = {}, None, []
    for l in lines:
        m = re.match(r"\[auto\] begin (\S+)", l)
        if m:
            name = m.group(1).replace("_on", "").replace("_off", "") if m.group(1).startswith("flashfail") else m.group(1)
            if name not in blocks:
                buf = []
                blocks[name] = buf
        if name:
            buf.append(l)
    for n, ls in blocks.items():
        p = os.path.join(OUT, f"{a.prefix}_auto_{n}_{stamp}.log")
        with open(p, "w", encoding="utf-8") as f:
            f.write(f"# scenario=auto_{n} (TEST AUTO, навантаження BTNSIM 700 + SNAP кожні 3,2 с)\n# {ver}\n")
            idx = max((i for i, l in enumerate(ls) if l.startswith("[auto] end")), default=0)
            body = ls[:idx] + ["# --- STATS final ---"] + ls[idx:]
            f.write("\n".join(body) + "\n")
        print("лог:", os.path.relpath(p))
    print("готово" if any(l.startswith("[auto] all done") for l in lines) else "НЕ ДОЧЕКАЛИСЬ кінця прогону")


if __name__ == "__main__":
    main()
