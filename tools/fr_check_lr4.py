#!/usr/bin/env python3
"""Автоматична перевірка тих FR, які не потребують рук (лампа, папір, відключення дротів): ЛР4, таблиця статусу FR.
Пише docs/metrics/lr4_fr_check_<час>.log. Прошивка на FreeRTOS (або будь-яка з тим самим CLI).
"""
import datetime
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab_serial import Board, parse_stat_lines  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "metrics")


def main():
    path = os.path.join(OUT, f"lr4_fr_check_{datetime.datetime.now():%Y%m%d_%H%M%S}.log")
    log = open(path, "w", encoding="utf-8")
    b = Board()
    res = []

    def say(s):
        log.write(s + "\n"); log.flush(); print(s)

    say("# " + b.version())
    b.send("SERVO OFF"); b.send("SKY FORCE AUTO")
    # FR-18: рядок tick щосекунди
    lines = b.read_lines(12.0)
    ticks = [l for l in lines if l.startswith("tick ")]
    res.append(("FR-18", f"рядків tick за 12 с: {len(ticks)}", 10 <= len(ticks) <= 14))
    # FR-01: частота опитування АЦП
    b.send("STATS RESET")
    b.read_lines(10.0)
    lines, _ = b.send("STATS", wait=3.0)
    st = parse_stat_lines(lines)
    n = st.get("adc", {}).get("n", 0)
    res.append(("FR-01", f"adc: {n} запусків за ≈10 с (потрібно ≈ 1000, тобто ≥ 100 Гц)", n >= 950))
    # FR-11, 14, 15: знімок за запитом, не частіше ніж раз на 3 с, номер кадру в журналі
    def next_frame():
        ls, _ = b.send("FRAMES", wait=2.0)
        m = next((re.search(r"next=(\d+)", l) for l in ls if "next=" in l), None)
        return int(m.group(1)) if m else -1
    f0 = next_frame()
    b.send("SNAP", wait=0.3); b.send("SNAP", wait=0.3)
    b.read_lines(7.0)
    f1 = next_frame()
    res.append(("FR-11, FR-15", f"два запити SNAP за < 1 с дали +{f1 - f0} кадр (next {f0} → {f1})", f1 - f0 == 1))
    ls, _ = b.send("LOG 6", wait=3.0)
    rec = [l for l in ls if "type=3" in l and f"frame={f0 & 0xFF}" in l]
    res.append(("FR-14, FR-15", f"запис журналу типу 3 (втрата сигналу) з номером кадру {f0 & 0xFF}: {'є' if rec else 'немає'}", bool(rec)))
    ls, _ = b.send(f"FRAMES {f0}", wait=2.5)
    res.append(("FR-15", f"кадр #{f0} читається з flash: {[l for l in ls if l.startswith('[frames]')][:1]}", any("mean=" in l for l in ls)))
    # FR-12: клас неба оновлюється камерою
    ls, _ = b.send("SKY", wait=2.0)
    res.append(("FR-12", f"{[l for l in ls if l.startswith('[prod] sky')][:1]}", any("frame=yes" in l for l in ls)))
    # FR-09: калібровка читається і змінюється командою
    ls, _ = b.send("CAL SHOW", wait=2.0)
    res.append(("FR-09", f"CAL SHOW: {[l for l in ls if l.startswith('[cal]')][:1]}", any("[cal]" in l for l in ls)))
    for fr, text, ok in res:
        say(f"{'OK  ' if ok else 'FAIL'} {fr}: {text}")
    say(f"# підсумок: {sum(1 for r in res if r[2])}/{len(res)} перевірок пройдено")
    log.close()


if __name__ == "__main__":
    main()
