#!/usr/bin/env python3
"""Затримка реакції на кнопку за записом логічного аналізатора + порівняння з DWT.

  ~/heliostat-venv/bin/python tools/analyze_latency.py docs/captures/lab3_button_latency.sr [docs/metrics/s3cap_*.log]

Канали: D0 = кнопка B1 (PC13, активний рівень 0), D1 = службовий пін PB3: імпульс на початку button_task (реакція),
D2 = PB5 захоплення кадру зайняте, D3 = PB6 знімок у flash зайнятий. Затримка = від першого спаду D0 до першого фронту D1 після нього.
Дребезг кнопки: спади D0, що йдуть один за одним ближче ніж 30 мс, вважаємо одним натисканням.
"""
import os
import re
import statistics
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from srlib import edges, load  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "docs", "lab_3", "img")
plt.rcParams.update({"font.size": 10, "axes.spines.top": False, "axes.spines.right": False})


def presses(data, rate):
    falls = [i for i, k in edges(data, 0) if k == "f"]
    out, last = [], None
    for i in falls:
        if last is None or (i - last) * 1000 / rate > 30:
            out.append(i)
        last = i
    return out


def busy_at(data, ch, idx):
    return (data[idx] >> ch) & 1


def main():
    path = sys.argv[1]
    data, rate = load(path)
    us = 1e6 / rate
    d1_rise = [i for i, k in edges(data, 1) if k == "r"]
    pr = presses(data, rate)
    # Одна реакція button_task обслуговує всі натискання, що накопичились до неї (подія зберігається в одній комірці):
    # затримка міряється від ПЕРШОГО необслуженого натискання, решта — злиті (втрачені) події.
    rows, prev = [], 0
    for r in d1_rise:
        group = [p for p in pr if prev < p <= r]
        if group:
            rows.append((group[0], (r - group[0]) * us, len(group) - 1))
        prev = r
    ok = [(p, l) for p, l, _ in rows]
    lat = [l for _, l in ok]
    merged = sum(m for _, _, m in rows)
    print(f"{path}: {len(data)/rate:.1f} с, натискань {len(pr)}, реакцій {len(rows)}, злитих (втрачених) натискань {merged}")
    if lat:
        lat_s = sorted(lat)
        p99 = lat_s[min(len(lat_s) - 1, int(0.99 * len(lat_s)))]
        print(f"затримка (аналізатор): min {min(lat):.0f} мкс, median {statistics.median(lat):.0f}, avg {statistics.mean(lat):.0f}, max {max(lat):.0f}, p99 {p99:.0f}")
    for i, (p, l, m) in enumerate(rows):
        cam, snap = busy_at(data, 2, p), busy_at(data, 3, p)
        print(f"  #{i+1:2d} t={p/rate:7.3f} с  затримка={l:10.0f} мкс  злито ще {m} натискань  камера={'зайнята' if cam else 'вільна'}, знімок={'зайнятий' if snap else 'вільний'}")
    # порівняння з DWT (btn_latency у логу прогону)
    if len(sys.argv) > 2:
        txt = open(sys.argv[2], encoding="utf-8", errors="replace").read()
        m = re.findall(r"STAT name=btn_latency n=(\d+) min_us=(\d+) avg_us=(\d+) max_us=(\d+)", txt)
        if m:
            n, mn, av, mx = (int(x) for x in m[-1])
            print(f"DWT у прошивці: n={n}, min {mn} мкс, avg {av}, max {mx}")
    # порівняння двох способів на тій самій метриці: тривалість захоплення кадру (D2) і знімка (D3) за аналізатором vs DWT
    for ch, name, stat in ((2, "захоплення кадру (D2)", "cam_frame"), (3, "знімок у flash (D3)", "snapshot")):
        e = edges(data, ch)
        w, st = [], None
        for i, k in e:
            if k == "r":
                st = i
            elif st is not None:
                w.append((i - st) * us); st = None
        if w:
            print(f"{name}: аналізатор n={len(w)}, min {min(w):.0f} мкс, avg {statistics.mean(w):.0f}, max {max(w):.0f}")
            if len(sys.argv) > 2:
                m = re.findall(rf"STAT name={stat} n=(\d+) min_us=(\d+) avg_us=(\d+) max_us=(\d+)", txt)
                if m:
                    n, mn, av, mx = (int(x) for x in m[-1])
                    print(f"   DWT у прошивці: n={n}, min {mn} мкс, avg {av}, max {mx}")
    if not lat:
        return
    # гістограма
    fig, ax = plt.subplots(figsize=(7, 3.6))
    ax.hist([l / 1000 for l in lat], bins=[0, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000], color="#2e86c1", edgecolor="white")
    ax.set_xscale("symlog", linthresh=1)
    ax.axvline(50, color="#c0392b", ls="--"); ax.text(52, ax.get_ylim()[1] * 0.9, "вимога PRD 50 мс", color="#c0392b", fontsize=8)
    ax.set_xlabel("затримка реакції на кнопку, мс (за аналізатором)"); ax.set_ylabel("кількість реакцій")
    ax.set_title(f"S3: {len(lat)} реакцій на {len(pr)} натискань (решта злилась), max {max(lat)/1000:.0f} мс", fontsize=10)
    fig.tight_layout(); fig.savefig(os.path.join(OUT, "fig_latency_hist.png"), dpi=150); plt.close(fig)
    # розмічені часові діаграми: найгірший і найкращий випадок
    def annotated(idx, name, title):
        p, l = ok[idx]
        t0, t1 = p - int(0.25 * rate), p + int(l / us) + int(0.5 * rate) if l > 200000 else p + int(0.003 * rate)
        if l <= 200000:
            t0 = p - int(0.0005 * rate); t1 = p + int(l / us) + int(0.0005 * rate)
        xs = [(i - p) * us / 1000 for i in range(t0, t1)]
        fig, axs = plt.subplots(4, 1, figsize=(9, 4.2), sharex=True)
        labels = ["D0 кнопка B1", "D1 реакція (PB3)", "D2 камера зайнята", "D3 знімок зайнятий"]
        for ch, ax in enumerate(axs):
            ys = [(data[i] >> ch) & 1 for i in range(t0, t1)]
            ax.step(xs, ys, where="post", color="#1f4e79", lw=1.2)
            ax.set_ylim(-0.2, 1.3); ax.set_yticks([0, 1]); ax.set_ylabel(labels[ch], rotation=0, ha="right", va="center", fontsize=8)
            ax.spines["left"].set_visible(False)
        axs[0].annotate("натискання", xy=(0, 0), xytext=(l / 1000 * 0.1 if l > 1000 else 0, 1.0), fontsize=8, arrowprops=dict(arrowstyle="->"))
        axs[1].annotate("", xy=(l / 1000, 1), xytext=(0, 1), arrowprops=dict(arrowstyle="<->", color="#c0392b"))
        axs[1].text(l / 2000, 1.12, f"затримка {l/1000:.3f} мс" if l < 1000 else f"затримка {l/1000:.0f} мс", color="#c0392b", ha="center", fontsize=9)
        axs[-1].set_xlabel("час від натискання, мс")
        axs[0].set_title(title, fontsize=10)
        fig.tight_layout(); fig.savefig(os.path.join(OUT, name), dpi=150); plt.close(fig)
    worst = max(range(len(ok)), key=lambda i: ok[i][1]); best = min(range(len(ok)), key=lambda i: ok[i][1])
    annotated(worst, "fig_latency_worst.png", f"Найгірший випадок: подія під час довгої операції ({ok[worst][1]/1000:.0f} мс)")
    annotated(best, "fig_latency_best.png", f"Найкращий випадок ({ok[best][1]:.0f} мкс)")
    print("рисунки: fig_latency_hist.png, fig_latency_worst.png, fig_latency_best.png")


if __name__ == "__main__":
    main()
