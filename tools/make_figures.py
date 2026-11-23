#!/usr/bin/env python3
"""Малює рисунки для звіту ЛР3 з реальних логів docs/metrics/ і memory.txt -> docs/lab_3/img/*.png.

  ~/heliostat-venv/bin/python tools/make_figures.py
"""
import glob
import os
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab_serial import parse_load, parse_stat_lines  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
METRICS = os.path.join(ROOT, "docs", "metrics")
OUT = os.path.join(ROOT, "docs", "lab_3", "img")
os.makedirs(OUT, exist_ok=True)
plt.rcParams.update({"font.size": 10, "axes.spines.top": False, "axes.spines.right": False})


def last_block(path):
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    idx = max((i for i, l in enumerate(lines) if l.startswith("# --- STATS")), default=0)
    return lines[idx:]


def newest(prefix):
    files = sorted(glob.glob(os.path.join(METRICS, prefix + "_*.log")))
    return files[-1] if files else None


def box(ax, x, y, w, h, text, fc="#e8eef7", ec="#4a6fa5", fs=9, bold=False):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.08", fc=fc, ec=ec, lw=1))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs, fontweight="bold" if bold else "normal")


def arrow(ax, x0, y0, x1, y1, text=None):
    ax.annotate("", xy=(x1, y1), xytext=(x0, y0), arrowprops=dict(arrowstyle="->", color="#555", lw=1.2))
    if text:
        ax.text((x0 + x1) / 2, (y0 + y1) / 2 + 0.12, text, ha="center", fontsize=8, color="#444")


def fig_tasks():
    fig, ax = plt.subplots(figsize=(11, 6.4))
    ax.set_xlim(0, 11); ax.set_ylim(0, 6.4); ax.axis("off")
    ax.text(5.5, 6.2, "Схема прошивки ЛР3: головний цикл (super loop) із часовим планувальником", ha="center", fontsize=12, fontweight="bold")
    # переривання
    box(ax, 0.2, 4.6, 2.2, 0.8, "EXTI кнопки B1 (PC13)\nмітка DWT + прапорець", "#fdebd0", "#b9770e")
    box(ax, 0.2, 3.5, 2.2, 0.8, "USART2 RX (IRQ)\nкільцевий буфер 256 Б", "#fdebd0", "#b9770e")
    box(ax, 0.2, 2.4, 2.2, 0.8, "SysTick 1 мс\nHAL_GetTick", "#fdebd0", "#b9770e")
    ax.text(1.3, 5.6, "Переривання", ha="center", fontsize=10, fontweight="bold")
    # цикл
    ax.add_patch(FancyBboxPatch((3.3, 0.4), 3.9, 5.4, boxstyle="round,pad=0.02,rounding_size=0.1", fc="#f4f6f7", ec="#2e4053", lw=1.5))
    ax.text(5.25, 5.55, "while(1): sched_run_once()", ha="center", fontsize=10, fontweight="bold")
    tasks = [("button 1 мс", "#fdebd0"), ("cli 1 мс", "#fdebd0"), ("adc 10 мс (FR-01)", "#d6eaf8"), ("control 10 мс (FR-02..06,13)", "#d6eaf8"),
             ("servo 20 мс (FR-03,04)", "#d6eaf8"), ("loss 100 мс (FR-14)", "#d6eaf8"), ("imu 100 мс (FR-17)", "#d6eaf8"),
             ("led 100 мс (FR-10)", "#d6eaf8"), ("log 5 мс (FR-07)", "#e8daef"), ("snapshot 100 мс (FR-14,15)", "#f5b7b1"),
             ("camera 3 с (FR-11,12,16)", "#f5b7b1"), ("tick 1 с (FR-18)", "#d6eaf8")]
    for i, (t, c) in enumerate(tasks):
        box(ax, 3.5, 5.0 - i * 0.4, 3.5, 0.33, t, c, "#7f8c8d", fs=8.5)
    ax.text(5.25, 0.15, "червоні — довгі блокувальні операції", ha="center", fontsize=8, color="#922b21")
    # пристрої
    ax.text(9.4, 5.6, "Пристрої (через Platform)", ha="center", fontsize=10, fontweight="bold")
    devs = [("Фоторезистори GM5528\nADC1, 4 канали", 4.6), ("Серво AZ/EL\nTIM3 PWM 50 Гц", 3.8), ("MPU-6050\nI²C1 100 кГц", 3.0),
            ("W25Q64 (журнал, кадри)\nSPI2 ~390 кГц", 2.2), ("OV7670\nSCCB + паралельна шина GPIO", 1.4), ("UART2 CLI / лог\n115200", 0.6)]
    for t, y in devs:
        box(ax, 8.2, y, 2.4, 0.65, t, "#d5f5e3", "#1e8449", fs=8.5)
    for x0, y0, y1 in [(2.4, 5.0, 5.0), (2.4, 3.9, 3.9)]:
        arrow(ax, x0, y0, 3.5, y1)
    for _, y in devs:
        arrow(ax, 7.0, 3.0, 8.2, y + 0.32)
    fig.savefig(os.path.join(OUT, "fig_tasks.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


def fig_scenarios():
    names = [("s1nocam", "S1 без камери"), ("s1", "S1 простій"), ("s2", "S2 номінальний"), ("s3", "S3 найгірший")]
    data = []
    for key, label in names:
        p = newest(key)
        if not p:
            continue
        blk = last_block(p)
        data.append((label, parse_stat_lines(blk), parse_load(blk)))
    if not data:
        return
    fig, axs = plt.subplots(1, 3, figsize=(12, 3.8))
    labels = [d[0] for d in data]
    vals = [[d[1].get("loop", {}).get("max_us", 0) / 1000 for d in data],
            [d[1].get("adc", {}).get("missed", 0) for d in data],
            [d[2].get("run_pct", 0) for d in data]]
    titles = ["Найдовша пауза головного циклу, мс", "Пропущені періоди ADC (FR-01) за 60 с", "Середнє завантаження CPU, %"]
    for ax, v, t in zip(axs, vals, titles):
        bars = ax.bar(range(len(v)), v, color=["#7fb3d5", "#5499c7", "#2e86c1", "#c0392b"][:len(v)])
        ax.set_xticks(range(len(v))); ax.set_xticklabels(labels, rotation=20, ha="right", fontsize=8)
        ax.set_title(t, fontsize=9)
        for b, x in zip(bars, v):
            ax.text(b.get_x() + b.get_width() / 2, b.get_height(), f"{x:.0f}", ha="center", va="bottom", fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "fig_scenarios.png"), dpi=150)
    plt.close(fig)


def points(path):
    rows = []
    for l in open(path, encoding="utf-8", errors="replace"):
        if l.startswith("# POINT"):
            d = dict(re.findall(r"(\w+)=(\d+)", l))
            rows.append({k: int(v) for k, v in d.items()})
    return rows


def fig_break():
    pa, pc = newest("break_adc"), newest("break_cam")
    if not (pa and pc):
        return
    a, c = points(pa), points(pc)
    fig, axs = plt.subplots(1, 2, figsize=(10, 3.8))
    axs[0].plot([r["period_ms"] for r in a], [r["adc_late_max_us"] / 1000 for r in a], "o-", color="#2e86c1")
    axs[0].set_xlabel("Період задачі ADC, мс"); axs[0].set_ylabel("Макс. запізнення старту, мс"); axs[0].invert_xaxis()
    axs[0].set_title("Підвищення частоти опитування ADC", fontsize=9)
    axs[1].plot([r["period_ms"] for r in c], [r["adc_missed"] for r in c], "o-", color="#c0392b")
    axs[1].set_xlabel("Період захоплення кадру, мс"); axs[1].set_ylabel("Пропущені періоди ADC за 20 с"); axs[1].invert_xaxis()
    axs[1].set_title("Підвищення частоти знімків камери", fontsize=9)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "fig_break.png"), dpi=150)
    plt.close(fig)


def fig_memory():
    p = os.path.join(METRICS, "memory.txt")
    if not os.path.exists(p):
        return
    txt = open(p, encoding="utf-8").read()
    m = re.search(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+\w+\s+build/firmware\.elf", txt, re.M)
    text_b, data_b, bss_b = (int(x) for x in m.groups()) if m else (0, 0, 0)
    syms = []
    for l in txt.split("лише змінні в RAM")[1].split("##")[0].splitlines():
        parts = l.split()
        if len(parts) == 4 and parts[0].isdigit():
            syms.append((parts[3], int(parts[1])))
    syms.sort(key=lambda s: s[1])
    fig, axs = plt.subplots(1, 2, figsize=(11, 3.8))
    axs[0].barh(["Flash (512 КБ)", "RAM (128 КБ)"], [(text_b + data_b) / 1024, (data_b + bss_b) / 1024], color=["#5499c7", "#c0392b"])
    for i, v in enumerate([(text_b + data_b) / 1024, (data_b + bss_b) / 1024]):
        axs[0].text(v, i, f" {v:.1f} КБ", va="center")
    axs[0].set_xlabel("КБ"); axs[0].set_title("Використання пам'яті (text+data / data+bss)", fontsize=9)
    axs[1].barh([s[0] for s in syms], [s[1] for s in syms], color="#7f8c8d")
    axs[1].set_xscale("log"); axs[1].set_xlabel("байтів (лог. шкала)"); axs[1].set_title("Найбільші змінні в RAM", fontsize=9)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "fig_memory.png"), dpi=150)
    plt.close(fig)


if __name__ == "__main__":
    fig_tasks(); fig_scenarios(); fig_break(); fig_memory()
    print("готово:", ", ".join(sorted(os.listdir(OUT))))
