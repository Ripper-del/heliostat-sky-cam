#!/usr/bin/env python3
"""Рисунки ЛР4 у docs/lab_4/img/: схема задач, порівняння з ЛР3, хронологія перемикань, пам'ять і стеки, вплив частоти тіка.

  python3 tools/make_figures_lr4.py            # усі рисунки, для яких є дані
  python3 tools/make_figures_lr4.py tasks      # лише схема задач (даних не потребує)
"""
import glob
import os
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_lr3_lr4 as C  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "lab_4", "img")
os.makedirs(OUT, exist_ok=True)
plt.rcParams["font.family"] = "Arial"
BLUE, RED, GREY, GREEN, ORANGE = "#3b7ab8", "#c0392b", "#7f8c8d", "#3a9a5b", "#e08a1e"


def box(ax, x, y, w, h, text, fc, ec="#34495e", fs=8.5, bold=False):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.05", fc=fc, ec=ec, lw=1.2))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs, weight="bold" if bold else "normal")


def arrow(ax, p, q, text="", color="#34495e", rad=0.0, dx=0, dy=0.0):
    ax.add_patch(FancyArrowPatch(p, q, arrowstyle="-|>", mutation_scale=11, lw=1.2, color=color, connectionstyle=f"arc3,rad={rad}"))
    if text:
        ax.text((p[0] + q[0]) / 2 + dx, (p[1] + q[1]) / 2 + 0.06 + dy, text, ha="center", fontsize=7.2, color=color)


def fig_tasks():
    fig, ax = plt.subplots(figsize=(13, 7.6))
    ax.set_xlim(0, 13.2); ax.set_ylim(-0.4, 7.6); ax.axis("off")
    ax.text(6.6, 7.3, "Схема задач FreeRTOS: пріоритети, черги, м'ютекси, переривання", ha="center", fontsize=13, weight="bold")
    ax.text(0.2, 6.75, "Переривання (NVIC 6, лише FromISR)", fontsize=9, weight="bold", color=GREY)
    ax.text(5.5, 6.75, "Задачі: ім'я  пріоритет  період  стек (слів)", fontsize=9, weight="bold", color=GREY)
    ax.text(9.6, 6.75, "Об'єкти ядра", fontsize=9, weight="bold", color=GREY)
    names = [("button", "P6  подія  112", "#fadbd8"), ("adc", "P5  10 мс  112", "#fadbd8"), ("control", "P4  10 мс  112", "#fcf3cf"), ("servo", "P4  20 мс  96", "#fcf3cf"),
             ("cli", "P3  подія  320", "#d5f5e3"), ("loss", "P2  100 мс  96", "#d5f5e3"), ("imu", "P2  100 мс  224", "#d5f5e3"), ("log", "P2  подія  256", "#d5f5e3"),
             ("tick", "P2  1 с  320", "#d5f5e3"), ("snapshot", "P1  подія  288", "#e8daef"), ("camera", "P1  3 с  224", "#e8daef"), ("led", "P1  100 мс  96", "#e8daef")]
    y = {}
    for i, (n, t, c) in enumerate(names):
        yy = 6.2 - i * 0.52
        y[n] = yy + 0.2
        box(ax, 5.5, yy, 3.0, 0.42, f"{n}   {t}", c, fs=8)
    # переривання і їхні об'єкти
    box(ax, 0.2, 5.5, 2.3, 0.7, "EXTI13: кнопка B1\n(TIM2 — програмна подія)", "#fdebd0")
    box(ax, 0.2, 4.2, 2.3, 0.7, "USART2 RX", "#fdebd0")
    box(ax, 0.2, 2.9, 2.3, 0.7, "SysTick 1 кГц\n(HAL tick + kernel tick)", "#fdebd0")
    box(ax, 3.1, 5.5, 1.8, 0.7, "q_btn\nчерга 16 × мітка DWT", "#d6eaf8", fs=8)
    box(ax, 3.1, 4.2, 1.8, 0.7, "sb_rx\nstream buffer 256 Б", "#d6eaf8", fs=8)
    arrow(ax, (2.5, 5.85), (3.1, 5.85)); arrow(ax, (4.9, 5.85), (5.5, y["button"]))
    arrow(ax, (2.5, 4.55), (3.1, 4.55)); arrow(ax, (4.9, 4.55), (5.5, y["cli"]))
    for n in ("adc", "control", "servo", "loss", "imu", "tick", "camera", "led"):
        arrow(ax, (2.5, 3.25), (5.5, y[n]), color="#bdc3c7", rad=0.0)
    ax.text(3.9, 3.05, "тік будить періодичні\nзадачі (vTaskDelayUntil)", fontsize=7.5, color=GREY, ha="center")
    # праві об'єкти
    box(ax, 9.7, 3.9, 1.9, 0.6, "q_log\nчерга 16 × запис журналу", "#d6eaf8", fs=7.8)
    box(ax, 9.7, 2.5, 1.9, 0.55, "mutex flash  (SPI2)", "#fdebd0", fs=8)
    box(ax, 9.7, 1.6, 1.9, 0.55, "mutex frame  (буфер кадру)", "#fdebd0", fs=8)
    box(ax, 9.7, 0.7, 1.9, 0.55, "mutex print  (printf)", "#fdebd0", fs=8)
    for n in ("control", "loss", "camera"):
        arrow(ax, (8.5, y[n]), (9.7, 4.2), color=BLUE, rad=0.0)
    arrow(ax, (9.7, 4.1), (8.5, y["log"]), color=BLUE)
    ax.text(11.8, 4.2, "log_event():\ncontrol, loss, camera\n→ q_log → log", fontsize=7.5, color=BLUE, va="center")
    for n in ("log", "snapshot", "cli"):
        arrow(ax, (8.5, y[n]), (9.7, 2.78), color="#a04000")
    for n in ("camera", "snapshot", "cli"):
        arrow(ax, (8.5, y[n]), (9.7, 1.88), color="#a04000", rad=0.0)
    for n in ("tick", "cli"):
        arrow(ax, (8.5, y[n]), (9.7, 0.98), color="#a04000")
    ax.text(11.8, 2.2, "порядок захоплення:\nframe → flash", fontsize=7.5, color="#a04000", va="center")
    ax.text(0.2, 2.25, "snap_request(): button, loss, control (PARK)\n→ xTaskNotifyGive(snapshot)", fontsize=8, va="top", color=GREY)
    ax.text(0.2, 1.25, "Спільні дані: q[4] і trk — критична секція\n(taskENTER_CRITICAL, < 10 мкс); g_frame — mutex frame.", fontsize=8, va="top", bbox=dict(fc="#fbfcfc", ec="#bdc3c7"))
    ax.text(0.2, -0.05, "Довгі операції (знімок, кадр) мають найнижчий пріоритет:\nїх витісняють усі інші задачі й переривання.", fontsize=8.5, va="top", color=RED)
    fig.savefig(os.path.join(OUT, "fig_rtos_tasks.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


def fig_compare():
    rows = {(r["metric"], r["scen"]): r for r in C.table_main()}
    items = [
        ("Затримка реакції на кнопку, max (S3), мс", rows.get(("Затримка реакції: від події до початку реакції, max", "S3")), 50, True),
        ("Джитер АЦП, max (S3), мс", rows.get(("Джитер АЦП (запізнення старту), max", "S3")), 2, True),
        ("Пропущені періоди АЦП за 60 с (S3)", rows.get(("Пропущені періоди АЦП за 60 с", "S3")), 0, True),
        ("Час відповіді на команду, max (S3), мс", rows.get(("Час відповіді на команду, max", "S3")), 100, True),
    ]
    items = [i for i in items if i[1] and i[1]["lr3"] is not None and i[1]["lr4"] is not None]
    if not items:
        return
    fig, axs = plt.subplots(1, len(items), figsize=(3.6 * len(items), 3.6))
    if len(items) == 1:
        axs = [axs]
    for ax, (title, r, req, _) in zip(axs, items):
        ax.bar(["super loop\n(ЛР3)", "FreeRTOS\n(ЛР4)"], [r["lr3"], r["lr4"]], color=[RED, BLUE], width=0.55)
        ax.set_yscale("symlog", linthresh=max(1e-3, req / 10 if req else 1))
        if req:
            ax.axhline(req, color="#555", ls="--", lw=1)
            ax.text(1.45, req, " вимога", fontsize=7, va="bottom", ha="right", color="#555")
        for i, v in enumerate([r["lr3"], r["lr4"]]):
            ax.text(i, v, f"{v:g}", ha="center", va="bottom", fontsize=8)
        ax.set_title(title, fontsize=8.5)
        ax.spines[["top", "right"]].set_visible(False)
    fig.suptitle("Super loop і FreeRTOS: максимум у найгіршому сценарії S3 (шкала логарифмічна)", fontsize=10)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "fig_compare.png"), dpi=150)
    plt.close(fig)


def fig_trace():
    """Хронологія перемикань задач за журналом TRACE: фрагмент S3 (кнопка під час знімка)."""
    files = sorted(glob.glob(os.path.join(C.M, "lr4_trace_*.log")))
    if not files:
        return
    ev = []
    for l in open(files[-1], encoding="utf-8"):
        m = re.match(r"TRACE t_us=(\d+) task=(\S+)", l)
        if m:
            ev.append((int(m.group(1)), m.group(2)))
    n1 = len(ev) // 2
    if n1 and ev[:n1] == ev[n1:]:         # лог пише кожен рядок двічі (автозапис Board і явний запис сценарію)
        ev = ev[:n1]
    ev = [e for i, e in enumerate(ev) if i == 0 or e != ev[i - 1]]      # або двічі поспіль
    if len(ev) < 20:
        return
    names = ["button", "adc", "control", "servo", "cli", "loss", "imu", "log", "tick", "snapshot", "camera", "led", "IDLE"]
    ypos = {n: i for i, n in enumerate(names)}
    cmap = {"button": RED, "adc": ORANGE, "control": "#d4ac0d", "servo": "#d4ac0d", "cli": GREEN, "snapshot": "#8e44ad", "camera": "#8e44ad", "IDLE": "#d5d8dc"}
    fig, ax = plt.subplots(figsize=(12, 4.6))
    for (t0, n), (t1, _) in zip(ev, ev[1:]):
        if n in ypos:
            ax.barh(ypos[n], max(t1 - t0, 2500) / 1000.0, left=t0 / 1000.0, height=0.7, color=cmap.get(n, BLUE), lw=0)   # коротші за 2,5 мс малюються шириною 2,5 мс, інакше їх не видно
    ax.set_yticks(range(len(names))); ax.set_yticklabels([f"{n}" for n in names], fontsize=8)
    ax.invert_yaxis(); ax.set_xlabel("час від початку журналу, мс (запуски, коротші за 2,5 мс, показано шириною 2,5 мс)"); ax.grid(axis="x", alpha=0.3)
    ax.set_title("Хронологія виконання задач у фрагменті S3 (журнал перемикань FreeRTOS): знімок витісняється подіями кнопки, АЦП і командами", fontsize=9.5)
    ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout(); fig.savefig(os.path.join(OUT, "fig_trace.png"), dpi=150); plt.close(fig)
    # збільшений фрагмент навколо події кнопки
    btn = [t for t, n in ev if n == "button"]
    if btn:
        c = btn[len(btn) // 2] / 1000.0
        fig, ax = plt.subplots(figsize=(12, 4.2))
        for (t0, n), (t1, _) in zip(ev, ev[1:]):
            if n in ypos and abs(t0 / 1000.0 - c) < 12:
                ax.barh(ypos[n], max(t1 - t0, 40) / 1000.0, left=t0 / 1000.0, height=0.7, color=cmap.get(n, BLUE), lw=0)
        ax.set_xlim(c - 8, c + 8)
        ax.set_yticks(range(len(names))); ax.set_yticklabels(names, fontsize=8); ax.invert_yaxis()
        ax.set_xlabel("мс"); ax.grid(axis="x", alpha=0.3)
        ax.set_title("Збільшено: подія кнопки будить button і snapshot; adc, imu, led витісняють snapshot (пріоритет 1)", fontsize=9.5)
        ax.spines[["top", "right"]].set_visible(False)
        fig.tight_layout(); fig.savefig(os.path.join(OUT, "fig_trace_zoom.png"), dpi=150); plt.close(fig)


def fig_stacks():
    files = sorted(glob.glob(os.path.join(C.M, "lr4_s3c_*.log")) or glob.glob(os.path.join(C.M, "lr4_s3*.log")))
    if not files:
        return
    d = C.load_log(files[-1])
    uniq = {}
    for s in d["stack"]:                      # у лозі рядки бувають подвоєні (автолог + явний запис): беремо по одному на задачу
        if s.get("size_words"):
            uniq[s["task"]] = s
    st = list(uniq.values())
    if not st:
        return
    st.sort(key=lambda s: -s["size_words"])
    fig, ax = plt.subplots(figsize=(8.5, 4.2))
    names = [s["task"] for s in st]
    used = [s["used_max_bytes"] for s in st]
    res = [s["size_words"] * 4 for s in st]
    ax.barh(names, res, color="#d6eaf8", label="зарезервовано")
    ax.barh(names, used, color=BLUE, label="використано (максимум за S3)")
    for i, (u, r) in enumerate(zip(used, res)):
        ax.text(r + 15, i, f"{u}/{r} Б", va="center", fontsize=7.5)
    ax.invert_yaxis(); ax.legend(fontsize=8, loc="lower right"); ax.set_xlim(0, max(res) * 1.2)
    ax.set_title("Стеки задач FreeRTOS: зарезервовано і використано (stack high water mark після S3)", fontsize=9.5)
    ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout(); fig.savefig(os.path.join(OUT, "fig_stacks.png"), dpi=150); plt.close(fig)


def fig_tick():
    out = {}
    for hz in (1000, 100):
        fs = sorted(glob.glob(os.path.join(C.M, "lr4_s3[abc]_*.log" if hz == 1000 else f"lr4_tick{hz}_s3*.log")))
        if fs:
            out[hz] = [C.load_log(f) for f in fs]
    if len(out) < 2:
        return
    fig, axs = plt.subplots(1, 3, figsize=(10.5, 3.4))
    def mx(rs, name, key):
        v = C.vals(rs, lambda r: r["stats"][name][key])
        return max(v) if v else 0
    for ax, (title, name, key, conv) in zip(axs, [("Джитер АЦП, max, мс", "adc", "late_max_us", 1000.0), ("Реакція на подію кнопки, max, мс", "evt_latency", "max_us", 1000.0), ("Відповідь на команду, max, мс", "cmd_latency", "max_us", 1000.0)]):
        vals = [mx(out[1000], name, key) / conv, mx(out[100], name, key) / conv]
        ax.bar(["1000 Гц", "100 Гц"], vals, color=[BLUE, ORANGE], width=0.5)
        for i, v in enumerate(vals):
            ax.text(i, v, f"{v:g}", ha="center", va="bottom", fontsize=8)
        ax.set_title(title, fontsize=9); ax.spines[["top", "right"]].set_visible(False)
    fig.suptitle("Вплив частоти тіка (configTICK_RATE_HZ) на S3", fontsize=10)
    fig.tight_layout(); fig.savefig(os.path.join(OUT, "fig_tick.png"), dpi=150); plt.close(fig)


if __name__ == "__main__":
    which = sys.argv[1:] or ["tasks", "compare", "trace", "stacks", "tick"]
    for w in which:
        {"tasks": fig_tasks, "compare": fig_compare, "trace": fig_trace, "stacks": fig_stacks, "tick": fig_tick}[w]()
    print("ok:", ", ".join(sorted(os.listdir(OUT))))
