#!/usr/bin/env python3
"""Порівняльна таблиця ЛР3 (super loop) і ЛР4 (FreeRTOS) за логами docs/metrics/{lr3,lr4}_*.log (ЛР4, п. 7.3).

  python3 tools/compare_lr3_lr4.py                  # таблиці в Markdown на stdout
  python3 tools/compare_lr3_lr4.py --json out.json  # ті самі числа машинно (для рисунків)

Однаковий метод для обох прошивок: ті самі сценарії (tools/measure_all.py), та сама кількість повторів, STAT/LOAD з прошивки (DWT).
Для метрик «max» береться максимум за всі повтори сценарію (вимога PRD перевіряється за найгіршим випадком), у дужках — по повторах.
"""
import argparse
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab_serial import kv, parse_load, parse_stat_lines  # noqa: E402

M = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "metrics")


def logs(fw, tag):
    """Усі логи fw_tag*.log (повтори a, b, c) — найновіший файл для кожного тега."""
    out = {}
    for p in sorted(glob.glob(os.path.join(M, f"{fw}_{tag}*.log"))):
        t = os.path.basename(p).split("_")[1]
        if re.fullmatch(tag + r"[a-c]?", t):
            out[t] = p                                   # пізніший файл перекриває ранній
    return list(out.values())


def load_log(p):
    lines = open(p, encoding="utf-8", errors="replace").read().splitlines()
    idx = max((i for i, l in enumerate(lines) if l.startswith("# --- STATS")), default=0)
    blk = lines[idx:]
    d = {"stats": parse_stat_lines(blk), "load": parse_load(blk), "lines": lines}
    d["tasks"] = {kv(l[5:]).get("name"): l for l in blk if l.startswith("TASK ")}
    d["task_kv"] = [kv(l[5:]) for l in blk if l.startswith("TASK ")]
    d["stack"] = [kv(l[6:]) for l in blk if l.startswith("STACK ")]
    d["heap"] = next((kv(l[5:]) for l in blk if l.startswith("HEAP ")), {})
    d["oscost"] = {}
    for l in blk:
        if l.startswith("OSCOST "):
            d["oscost"].update(kv(l[7:]))
    mem = next((l for l in blk if l.startswith("# MEM")), "")
    d["mem"] = {k: v for k, v in re.findall(r"'(\w+)': (\d+)", mem)}
    return d


def runs(fw, tag):
    return [load_log(p) for p in logs(fw, tag)]


def vals(rs, fn):
    out = []
    for r in rs:
        try:
            v = fn(r)
        except Exception:
            v = None
        if v is not None:
            out.append(v)
    return out


def stat(name, key):
    return lambda r: r["stats"][name][key]


def fmt_runs(v, unit=""):
    if not v:
        return "—"
    return f"{max(v):g}{unit} ({', '.join(f'{x:g}' for x in v)})" if len(v) > 1 else f"{v[0]:g}{unit}"


def ms(us):
    return us / 1000.0


def table_main():
    rows = []

    def row(metric, scen, tag, fn, req, tool, unit="", conv=None, kind="max", better_low=True, req_val=None):
        a, b = runs("lr3", tag), runs("lr4", tag)
        va, vb = vals(a, fn), vals(b, fn)
        if conv:
            va, vb = [conv(x) for x in va], [conv(x) for x in vb]
        agg = max if kind == "max" else (lambda x: sum(x) / len(x))
        ra, rb = (agg(va) if va else None), (agg(vb) if vb else None)
        rows.append(dict(metric=metric, scen=scen, lr3=ra, lr4=rb, lr3_runs=va, lr4_runs=vb, req=req, tool=tool, unit=unit, req_val=req_val, better_low=better_low))

    row("Затримка реакції: від події до початку реакції, max", "S3", "s3", stat("evt_latency", "max_us"), "≤ 50 мс", "DWT + апаратний момент події (TIM2)", " мс", ms, req_val=50)
    row("Затримка реакції: від події до початку реакції, середня", "S3", "s3", stat("evt_latency", "avg_us"), "—", "DWT + апаратний момент події", " мс", ms, kind="avg")
    row("Затримка реакції: від входу в ISR до початку реакції, max", "S3", "s3", stat("btn_latency", "max_us"), "—", "DWT (як у ЛР3)", " мс", ms)
    row("Подій кнопки оброблено / втрачено", "S3", "s3", lambda r: r["load"].get("btn_lost"), "0", "лічильник", "", None, req_val=0)
    row("Джитер АЦП (запізнення старту), max", "S3", "s3", stat("adc", "late_max_us"), "≤ 2 мс", "DWT", " мс", ms, req_val=2)
    row("Джитер АЦП, max", "S2", "s2", stat("adc", "late_max_us"), "≤ 2 мс", "DWT", " мс", ms, req_val=2)
    row("Пропущені періоди АЦП за 60 с", "S3", "s3", stat("adc", "missed"), "0", "DWT", "", None, req_val=0)
    row("Пропущені періоди АЦП за 60 с", "S2", "s2", stat("adc", "missed"), "0", "DWT", "", None, req_val=0)
    row("Джитер IMU, max", "S3", "s3", stat("imu", "late_max_us"), "≤ 2 мс", "DWT", " мс", ms, req_val=2)
    row("Час відповіді на команду, max", "S3", "s3", stat("cmd_latency", "max_us"), "≤ 100 мс (FR-09)", "DWT: Enter у ISR → початок команди", " мс", ms, req_val=100)
    row("Втрачені байти UART", "S3", "s3", lambda r: r["load"].get("uart_rx_overflow"), "0", "лічильник", "", None, req_val=0)
    row("Захоплення кадру (довга операція), max", "S3", "s3", stat("cam_frame", "max_us"), "—", "DWT", " мс", ms)
    row("Знімок у flash (кадр + запис), max", "S3", "s3", stat("snapshot", "max_us"), "—", "DWT", " мс", ms)
    row("Завантаження CPU (середнє за прогін)", "S1", "s1", lambda r: r["load"].get("run_pct"), "—", "DWT", " %", None, kind="avg")
    row("Завантаження CPU (середнє за прогін)", "S2", "s2", lambda r: r["load"].get("run_pct"), "—", "DWT", " %", None, kind="avg")
    row("Завантаження CPU (середнє за прогін)", "S3", "s3", lambda r: r["load"].get("run_pct"), "—", "DWT", " %", None, kind="avg")
    row("Помилки захоплення кадру (cam_err)", "S3", "s3", lambda r: r["load"].get("cam_err"), "0", "лічильник", "", None, req_val=0)
    row("Втрачені записи журналу (pend_lost)", "S3", "s3", lambda r: r["load"].get("pend_lost"), "0", "лічильник", "", None, req_val=0)
    return rows


def verdict(r):
    a, b = r["lr3"], r["lr4"]
    if a is None or b is None:
        return "—"
    rv = r.get("req_val")
    if rv is not None:
        oka, okb = a <= rv, b <= rv
        if oka and okb:
            return "обидва виконують"
        if okb and not oka:
            return "FreeRTOS"
        if oka and not okb:
            return "super loop"
        return "жоден не виконує" if abs(a - b) < 1e-9 else ("FreeRTOS (вимога не виконана)" if b < a else "super loop (вимога не виконана)")
    if abs(a - b) < 1e-9:
        return "однаково"
    return "FreeRTOS" if (b < a) == r["better_low"] else "super loop"


def md_main(rows):
    out = ["| Метрика | Сценарій | Super loop (ЛР3) | FreeRTOS (ЛР4) | Різниця | Вимога PRD | Інструмент | Хто кращий |", "|---|---|---|---|---|---|---|---|"]
    for r in rows:
        a, b = r["lr3"], r["lr4"]
        if a is None or b is None:
            diff = "—"
        else:
            d = b - a
            ratio = (a / b) if b else None
            diff = f"{d:+.2f}{r['unit']}" + (f", ÷{ratio:.0f}" if ratio and ratio >= 2 else (f", ×{b / a:.1f}" if a and b / a >= 2 else ""))
        out.append(f"| {r['metric']} | {r['scen']} | {fmt_runs(r['lr3_runs'], r['unit'])} | {fmt_runs(r['lr4_runs'], r['unit'])} | {diff} | {r['req']} | {r['tool']} | {verdict(r)} |")
    return "\n".join(out)


def md_tasks(tag):
    a, b = runs("lr3", tag), runs("lr4", tag)
    if not a or not b:
        return ""
    names = [n for n in a[-1]["stats"] if "late_max_us" in a[-1]["stats"][n]]
    out = [f"| Задача | LR3 запізнення max, мкс | LR4 запізнення max, мкс | LR3 пропущено | LR4 пропущено | LR3 час виконання max, мкс | LR4 час виконання max, мкс |", "|---|---|---|---|---|---|---|"]
    for n in names:
        def agg(rs, key):
            v = vals(rs, lambda r: r["stats"][n][key])
            return max(v) if v else None
        def tot(rs, key):
            v = vals(rs, lambda r: r["stats"][n][key])
            return sum(v) if v else None
        out.append(f"| {n} | {agg(a, 'late_max_us')} | {agg(b, 'late_max_us')} | {tot(a, 'missed')} | {tot(b, 'missed')} | {agg(a, 'max_us')} | {agg(b, 'max_us')} |")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    a = ap.parse_args()
    rows = table_main()
    print(md_main(rows))
    for tag in ("s3", "s2"):
        t = md_tasks(tag)
        if t:
            print(f"\n### Задачі, {tag.upper()} (максимум / сума за повтори)\n\n{t}")
    if a.json:
        json.dump(rows, open(a.json, "w"), ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
