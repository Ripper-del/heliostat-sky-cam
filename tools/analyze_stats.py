#!/usr/bin/env python3
"""Розбір логів docs/metrics/*.log у таблиці Markdown.

  python3 tools/analyze_stats.py docs/metrics/s3_*.log          # таблиця задач для кожного логу
  python3 tools/analyze_stats.py --compare s1.log s2.log s3.log # зведена таблиця S1/S2/S3 по ключових метриках
Береться ОСТАННІЙ блок STATS у файлі.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab_serial import parse_load, parse_stat_lines  # noqa: E402


def last_block(path):
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    idx = max((i for i, l in enumerate(lines) if l.startswith("# --- STATS")), default=0)
    return lines, lines[idx:]


def header_info(lines):
    ver = next((l[2:] for l in lines if l.startswith("# VER")), "VER unknown")
    sc = next((l for l in lines if l.startswith("# scenario=")), "")
    return ver, sc


def table(path):
    lines, block = last_block(path)
    stats, load = parse_stat_lines(block), parse_load(block)
    ver, sc = header_info(lines)
    out = [f"### {os.path.basename(path)}", "", f"`{ver}`  {sc}", ""]
    out.append("| Задача | n | min, мкс | avg, мкс | max, мкс | запізнення max, мкс | запізнення p99, мкс | пропущено |")
    out.append("|---|---|---|---|---|---|---|---|")
    for name, s in stats.items():
        if "late_max_us" in s:
            out.append(f"| {name} | {s['n']} | {s['min_us']} | {s['avg_us']} | {s['max_us']} | {s['late_max_us']} | {s['late_p99_us']} | {s['missed']} |")
    loop = stats.get("loop", {})
    if loop:
        out.append("")
        out.append(f"Період головного циклу: min {loop['min_us']} / avg {loop['avg_us']} / max {loop['max_us']} / p99 {loop.get('p99_us')} мкс. "
                   f"Завантаження CPU: середнє за прогін {load.get('run_pct')} %, останнє вікно 1 с {load.get('pct')} %. Втрачено байтів UART: {load.get('uart_rx_overflow')}.")
    extra = [l for l in lines if l.startswith("# S3") or l.startswith("# FAIL")]
    if extra:
        out.append("")
        out += ["- " + e[2:] for e in extra]
    return "\n".join(out) + "\n"


def compare(paths):
    cols, data = [], []
    for p in paths:
        lines, block = last_block(p)
        stats, load = parse_stat_lines(block), parse_load(block)
        cols.append(os.path.basename(p).split("_")[0].upper())
        data.append((stats, load))
    metrics = [
        ("Період циклу, max, мкс", lambda s, l: s.get("loop", {}).get("max_us")),
        ("Період циклу, p99, мкс", lambda s, l: s.get("loop", {}).get("p99_us")),
        ("ADC (FR-01): запізнення max, мкс", lambda s, l: s.get("adc", {}).get("late_max_us")),
        ("ADC: пропущені періоди", lambda s, l: s.get("adc", {}).get("missed")),
        ("MPU: запізнення max, мкс", lambda s, l: s.get("imu", {}).get("late_max_us")),
        ("Кадр камери, max, мкс", lambda s, l: s.get("cam_frame", {}).get("max_us")),
        ("Знімок у flash, max, мкс", lambda s, l: s.get("snapshot", {}).get("max_us")),
        ("Реакція на кнопку, max, мкс", lambda s, l: s.get("btn_latency", {}).get("max_us")),
        ("Реакція на кнопку, n", lambda s, l: s.get("btn_latency", {}).get("n")),
        ("Середнє завантаження CPU за прогін, %", lambda s, l: l.get("run_pct")),
        ("Затримка відповіді на команду, max, мкс", lambda s, l: s.get("cmd_latency", {}).get("max_us")),
        ("Втрачено натискань кнопки", lambda s, l: l.get("btn_lost")),
        ("Втрачено байтів UART", lambda s, l: l.get("uart_rx_overflow")),
    ]
    out = ["| Метрика | " + " | ".join(cols) + " |", "|---|" + "---|" * len(cols)]
    for name, fn in metrics:
        out.append(f"| {name} | " + " | ".join(str(fn(s, l)) if fn(s, l) is not None else "-" for s, l in data) + " |")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--compare", action="store_true")
    a = ap.parse_args()
    print(compare(a.logs) if a.compare else "\n".join(table(p) for p in a.logs))


if __name__ == "__main__":
    main()
