#!/usr/bin/env python3
"""Рисунки ЛР5 (docs/lab_5/img/): порівняння трьох підходів у S3, AO-власник проти м'ютекса,
стеки AO, фрагмент запису логічного аналізатора. Числа беруться з логів docs/metrics/ і запису docs/captures/."""
import glob
import os
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import srlib  # noqa: E402

ROOT = os.path.join(HERE, "..")
MET = os.path.join(ROOT, "docs", "metrics")
OUT = os.path.join(ROOT, "docs", "lab_5", "img")
FW = [("lr3", "Super loop (ЛР3)", "#c0392b"), ("lr4", "FreeRTOS (ЛР4)", "#2980b9"), ("lr5", "QP/C (ЛР5)", "#27ae60")]


def last_stats(path):
    """Рядки STAT/LOAD останнього зрізу STATS у лозі."""
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    idx = max((i for i, l in enumerate(lines) if l.startswith("# --- STATS")), default=0)
    st = {}
    for l in lines[idx:]:
        m = re.match(r"STAT name=(\S+) (.*)", l)
        if m:
            st[m.group(1)] = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", m.group(2))}
        if l.startswith("LOAD "):
            st["LOAD"] = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", l)}
        if l.startswith("STACK task="):
            d = dict(re.findall(r"(\w+)=(\w+)", l))
            st.setdefault("STACK", {})[d["task"]] = (int(d["size_words"]) * 4, int(d["used_max_bytes"]))
    return st


def s3_logs(fw):
    from compare_lr3_lr4 import logs          # той самий вибір логів матриці, що в compare_all.py
    return logs(fw, "s3")


def fig_compare():
    rows = [("Реакція\nна подію, мс", lambda s: s["evt_latency"]["max_us"] / 1000),
            ("Відповідь\nна команду, мс", lambda s: s["cmd_latency"]["max_us"] / 1000),
            ("Запізнення\nАЦП, мс", lambda s: s["adc"]["late_max_us"] / 1000),
            ("Запізнення\nIMU, мс", lambda s: s["imu"]["late_max_us"] / 1000),
            ("Знімок\nу flash, мс", lambda s: s["snapshot"]["max_us"] / 1000)]
    fig, ax = plt.subplots(figsize=(9, 4.2))
    w = 0.26
    for k, (fw, name, col) in enumerate(FW):
        sts = [last_stats(p) for p in s3_logs(fw)]
        vals = [max(f(s) for s in sts) for _, f in rows]
        xs = np.arange(len(rows)) + (k - 1) * w
        ax.bar(xs, vals, w, label=name, color=col)
        for x, v in zip(xs, vals):
            ax.text(x, v * 1.08, (f"{v:.0f}" if v >= 10 else f"{v:.2f}"), ha="center", va="bottom", fontsize=7)
    ax.set_yscale("log")
    ax.set_ylim(0.1, 1e5)
    ax.set_xticks(np.arange(len(rows)))
    ax.set_xticklabels([r[0] for r in rows], fontsize=8)
    ax.set_title("S3, максимум за три прогони (лог. шкала)", fontsize=10)
    ax.legend(fontsize=8, loc="upper center", ncol=3)
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "fig_compare3.png"), dpi=150)
    plt.close(fig)


def fig_owner_mutex():
    def fw_max(pattern):
        fs = sorted(glob.glob(os.path.join(MET, pattern)))
        return last_stats(fs[-1])["flash_wait"]["max_us"] / 1000
    cases = [("lograte", "запис кожні 50 мс"), ("inv", "+ imu займає CPU 50 мс")]
    own = [fw_max(f"lr5_auto_{c}_*.log") for c, _ in cases]
    mtx = [fw_max(f"lr5mx_auto_{c}_*.log") for c, _ in cases]
    fig, ax = plt.subplots(figsize=(7, 3.6))
    xs = np.arange(len(cases))
    ax.bar(xs - 0.18, mtx, 0.36, label="м'ютекс flash + очікування з блокуванням (як ЛР4)", color="#2980b9")
    ax.bar(xs + 0.18, own, 0.36, label="AO-власник log, кроки по 64 Б", color="#27ae60")
    for x, v in zip(xs - 0.18, mtx):
        ax.text(x, v * 1.1, f"{v:.1f}", ha="center", fontsize=8)
    for x, v in zip(xs + 0.18, own):
        ax.text(x, v * 1.1, f"{v:.1f}", ha="center", fontsize=8)
    ax.set_yscale("log")
    ax.set_ylim(1, 5000)
    ax.set_xticks(xs)
    ax.set_xticklabels([c[1] for c in cases], fontsize=8)
    ax.set_ylabel("очікування запису журналу, max, мс", fontsize=8)
    ax.set_title("TEST AUTO, 30 с на сценарій, BTNSIM 700 + знімок кожні 3,2 с", fontsize=9)
    ax.legend(fontsize=7.5, loc="upper left")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "fig_owner_mutex.png"), dpi=150)
    plt.close(fig)


def fig_stacks():
    st = last_stats(s3_logs("lr5")[0])["STACK"]
    names = sorted(st, key=lambda n: -st[n][0])
    fig, ax = plt.subplots(figsize=(8, 3.4))
    xs = np.arange(len(names))
    ax.bar(xs, [st[n][0] for n in names], 0.6, color="#d5dbdb", label="зарезервовано")
    ax.bar(xs, [st[n][1] for n in names], 0.6, color="#27ae60", label="використано max (S3)")
    ax.set_xticks(xs)
    ax.set_xticklabels(names, fontsize=8, rotation=30)
    ax.set_ylabel("байти", fontsize=8)
    ax.legend(fontsize=8)
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "fig_stacks_lr5.png"), dpi=150)
    plt.close(fig)


def fig_analyzer():
    data, rate = srlib.load(os.path.join(ROOT, "docs", "captures", "lab5_button_latency.sr"))
    a = np.frombuffer(data, dtype=np.uint8)
    d3 = (a >> 3) & 1
    d1 = (a >> 1) & 1
    # вікно: перший знімок (D3), усередині якого є реакція (D1)
    rise3 = np.flatnonzero(np.diff(d3.astype(np.int8)) == 1)
    fall3 = np.flatnonzero(np.diff(d3.astype(np.int8)) == -1)
    rise1 = np.flatnonzero(np.diff(d1.astype(np.int8)) == 1)
    t0 = t1 = None
    for r in rise3:
        f = fall3[fall3 > r]
        if len(f) and np.any((rise1 > r) & (rise1 < f[0])):
            t0, t1 = r - int(0.3 * rate), f[0] + int(0.3 * rate)
            break
    if t0 is None:
        t0, t1 = 0, int(4 * rate)
    seg = a[max(t0, 0):t1]
    step = max(1, len(seg) // 20000)
    t = np.arange(0, len(seg), step) / rate * 1000
    names = ["D0 кнопка B1", "D1 реакція button", "D2 кадр (camera)", "D3 знімок (log пише кадр)"]
    fig, ax = plt.subplots(figsize=(9, 3.2))
    for ch in range(4):
        v = (seg[::step] >> ch) & 1
        lvl = (3 - ch) * 1.5
        # max у кожному відрізку, щоб короткі імпульси D1 не зникли при проріджуванні
        full = (seg >> ch) & 1
        n = len(full) // step
        vm = full[: n * step].reshape(n, step).max(axis=1) if ch == 1 else v[:n]
        ax.step(t[:n], lvl + vm * 1.0, where="post", lw=0.9, color=["#7f8c8d", "#c0392b", "#2980b9", "#27ae60"][ch])
        ax.text(-5, lvl + 0.4, names[ch], ha="right", va="center", fontsize=8)
    ax.set_yticks([])
    ax.set_xlabel("мс від початку фрагмента", fontsize=8)
    ax.set_xlim(0, t[-1])
    ax.set_title("ЛР5, S3: реакція на кнопку під час знімка (логічний аналізатор, 1 МГц)", fontsize=9)
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "fig_analyzer_s3_lr5.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    fig_compare()
    fig_owner_mutex()
    fig_stacks()
    fig_analyzer()
    print("рисунки:", OUT)
