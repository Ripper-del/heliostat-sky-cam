#!/usr/bin/env python3
"""ЛР5, п. 9.4: фінальна таблиця трьох підходів за логами docs/metrics/{lr3,lr4,lr5}_*.log.

  python3 tools/compare_all.py                  # таблиці в Markdown на stdout
  python3 tools/compare_all.py --json out.json  # ті самі числа машинно (для рисунків)

Той самий метод, що в compare_lr3_lr4.py (ЛР4): ті самі сценарії (tools/measure_all.py), той самий інструмент (STAT/LOAD з прошивки,
DWT), для «max» — максимум за повтори, у дужках — по повторах.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_lr3_lr4 import fmt_runs, ms, runs, stat, vals  # noqa: E402

FW = ("lr3", "lr4", "lr5")
NAME = {"lr3": "super loop", "lr4": "FreeRTOS", "lr5": "QP/C"}


def table():
    rows = []

    def row(metric, scen, tag, fn, req, tool, unit="", conv=None, kind="max", better_low=True, req_val=None):
        r = dict(metric=metric, scen=scen, req=req, tool=tool, unit=unit, req_val=req_val, better_low=better_low)
        agg = max if kind == "max" else (lambda x: sum(x) / len(x))
        for fw in FW:
            v = vals(runs(fw, tag), fn)
            if conv:
                v = [conv(x) for x in v]
            r[fw + "_runs"] = v
            r[fw] = agg(v) if v else None
        rows.append(r)

    row("Затримка реакції на подію кнопки, max", "S3", "s3", stat("evt_latency", "max_us"), "≤ 50 мс (FR-11)", "DWT + апаратний момент події (TIM2)", " мс", ms, req_val=50)
    row("Затримка реакції на подію кнопки, середня", "S3", "s3", stat("evt_latency", "avg_us"), "—", "DWT", " мс", ms, kind="avg")
    row("Подій кнопки втрачено", "S3", "s3", lambda r: r["load"].get("btn_lost"), "0", "лічильник", "", None, req_val=0)
    row("Джитер АЦП, max", "S3", "s3", stat("adc", "late_max_us"), "≤ 2 мс", "DWT", " мс", ms, req_val=2)
    row("Джитер АЦП, max", "S2", "s2", stat("adc", "late_max_us"), "≤ 2 мс", "DWT", " мс", ms, req_val=2)
    row("Пропущені періоди АЦП за 60 с", "S3", "s3", stat("adc", "missed"), "0", "DWT", "", None, req_val=0)
    row("Джитер керування (control), max", "S3", "s3", stat("control", "late_max_us"), "≤ 10 мс", "DWT", " мс", ms, req_val=10)
    row("Джитер IMU, max", "S3", "s3", stat("imu", "late_max_us"), "≤ 2 мс", "DWT", " мс", ms, req_val=2)
    row("Пропущені періоди IMU за 60 с", "S3", "s3", stat("imu", "missed"), "0", "DWT", "", None, req_val=0)
    row("Час відповіді на команду, max", "S3", "s3", stat("cmd_latency", "max_us"), "≤ 100 мс (FR-09)", "DWT: Enter у ISR → початок команди", " мс", ms, req_val=100)
    row("Втрачені байти UART", "S3", "s3", lambda r: r["load"].get("uart_rx_overflow"), "0", "лічильник", "", None, req_val=0)
    row("Очікування запису журналу у flash, max", "S3", "s3", stat("flash_wait", "max_us"), "—", "DWT: LOG_REQ → запис у flash (лише ЛР5)", " мс", ms)
    row("Захоплення кадру (довга операція), max", "S3", "s3", stat("cam_frame", "max_us"), "—", "DWT", " мс", ms)
    row("Знімок у flash (кадр + запис), max", "S3", "s3", stat("snapshot", "max_us"), "—", "DWT", " мс", ms)
    row("Завантаження CPU (середнє за прогін)", "S1", "s1", lambda r: r["load"].get("run_pct"), "—", "DWT", " %", None, kind="avg")
    row("Завантаження CPU (середнє за прогін)", "S2", "s2", lambda r: r["load"].get("run_pct"), "—", "DWT", " %", None, kind="avg")
    row("Завантаження CPU (середнє за прогін)", "S3", "s3", lambda r: r["load"].get("run_pct"), "—", "DWT", " %", None, kind="avg")
    row("Помилки захоплення кадру (cam_err)", "S3", "s3", lambda r: r["load"].get("cam_err"), "0", "лічильник", "", None, req_val=0)
    row("Помилки flash (flash_err)", "S3", "s3", lambda r: r["load"].get("flash_err"), "0", "лічильник", "", None, req_val=0)
    row("Втрачені записи журналу (pend_lost)", "S3", "s3", lambda r: r["load"].get("pend_lost"), "0", "лічильник", "", None, req_val=0)
    return rows


def verdict(r):
    v = {fw: r[fw] for fw in FW if r[fw] is not None}
    if len(v) < 2:
        return "—"
    rv = r.get("req_val")
    if rv is not None:
        ok = [fw for fw in v if v[fw] <= rv]
        if len(ok) == len(v):
            return "усі виконують"
        if not ok:
            return "жоден не виконує"
        return ", ".join(NAME[f] for f in ok)
    best = min(v.values()) if r["better_low"] else max(v.values())
    winners = [fw for fw in v if abs(v[fw] - best) <= max(1e-9, abs(best) * 0.1)]
    return "≈ " + ", ".join(NAME[f] for f in winners) if len(winners) > 1 else NAME[winners[0]]


def md(rows):
    out = ["| Метрика | Сценарій | Super loop (ЛР3) | FreeRTOS (ЛР4) | QP/C на FreeRTOS (ЛР5) | Вимога PRD | Інструмент | Кращий |",
           "|---|---|---|---|---|---|---|---|"]
    for r in rows:
        out.append(f"| {r['metric']} | {r['scen']} | {fmt_runs(r['lr3_runs'], r['unit'])} | {fmt_runs(r['lr4_runs'], r['unit'])} | "
                   f"{fmt_runs(r['lr5_runs'], r['unit'])} | {r['req']} | {r['tool']} | {verdict(r)} |")
    return "\n".join(out)


def md_tasks(tag):
    rs = {fw: runs(fw, tag) for fw in FW}
    if not all(rs.values()):
        return ""
    names = [n for n in rs["lr5"][-1]["stats"] if "late_max_us" in rs["lr5"][-1]["stats"][n]]
    out = ["| Задача / AO | Запізнення max, мкс (ЛР3 / ЛР4 / ЛР5) | Пропущено (ЛР3 / ЛР4 / ЛР5) | Час кроку max, мкс (ЛР3 / ЛР4 / ЛР5) |", "|---|---|---|---|"]
    for n in names:
        def agg(r, key, f=max):
            v = vals(r, lambda x: x["stats"][n][key])
            return f(v) if v else "—"
        late = " / ".join(str(agg(rs[fw], "late_max_us")) for fw in FW)
        miss = " / ".join(str(agg(rs[fw], "missed", sum)) for fw in FW)
        ex = " / ".join(str(agg(rs[fw], "max_us")) for fw in FW)
        out.append(f"| {n} | {late} | {miss} | {ex} |")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    a = ap.parse_args()
    rows = table()
    print(md(rows))
    for tag in ("s3", "s2"):
        t = md_tasks(tag)
        if t:
            print(f"\n### Задачі й AO, {tag.upper()} (максимум / сума за повтори)\n\n{t}")
    if a.json:
        json.dump(rows, open(a.json, "w"), ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
