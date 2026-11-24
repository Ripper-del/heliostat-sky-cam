#!/usr/bin/env python3
"""Короткий підсумок запису аналізатора по каналах: фронти, частка високого рівня, ширини імпульсів.

  python3 tools/sr_summary.py docs/captures/lab3_wiring_check.sr
Корисно, щоб перевірити підключення (який канал що показує) до розбору в PulseView.
"""
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from srlib import edges, load  # noqa: E402


def main():
    data, rate = load(sys.argv[1])
    print(f"{sys.argv[1]}: {len(data)} відліків, {rate/1e6:g} МГц, {len(data)/rate:.2f} с")
    for ch in range(8):
        e = edges(data, ch)
        high = sum((b >> ch) & 1 for b in data) / len(data) * 100
        pulses = []
        start = None
        for i, kind in e:
            if kind == "r":
                start = i
            elif start is not None:
                pulses.append((i - start) * 1e6 / rate)
                start = None
        line = f"D{ch}: фронтів {len(e):6d}, high {high:5.1f} %"
        if pulses:
            line += f", імпульсів {len(pulses)}, ширина мкс min/med/max = {min(pulses):.0f}/{statistics.median(pulses):.0f}/{max(pulses):.0f}"
            if len(e) >= 4:
                rises = [i for i, k in e if k == "r"]
                gaps = [(b - a) * 1e6 / rate for a, b in zip(rises, rises[1:])]
                line += f", період мкс med = {statistics.median(gaps):.0f}"
        print(line)


if __name__ == "__main__":
    main()
