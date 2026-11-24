#!/usr/bin/env python3
"""Метрики складності ЛР4 (п. 7.3): рядки коду в прикладному шарі, змінні, що пишуть ISR, місця правок при додаванні задачі.
Один спосіб підрахунку для обох версій: непорожні рядки (усього) і непорожні рядки без суто коментарних (код).
LR3 береться з git-тега lab3 (`git show lab3:<файл>`), LR4 — з робочого дерева.
"""
import re
import subprocess
import sys

LR3 = ["firmware/App/Src/product.c", "firmware/App/Src/sched.c", "firmware/App/Inc/sched.h", "firmware/App/Inc/product.h"]
LR4 = ["Core/Src/product_rtos.c", "Core/Src/rtos_port.c", "Core/Inc/rtos_port.h", "App/Inc/product.h"]
LR4 = ["firmware/" + p for p in LR4]
EXTRA4 = {"rtstats (облік CPU і журнал перемикань)": ["firmware/App/Inc/rtstats.h", "firmware/App/Src/rtstats.c"],
          "rtos_tests (сценарії п. 7.2, лише діагностика)": ["firmware/Core/Src/rtos_tests.c", "firmware/Core/Inc/rtos_tests.h"],
          "FreeRTOSConfig.h": ["firmware/Core/Inc/FreeRTOSConfig.h"]}


def lines(ref, path):
    if ref == "wt":
        try:
            return open(path, encoding="utf-8").read().splitlines()
        except FileNotFoundError:
            return []
    r = subprocess.run(["git", "show", f"{ref}:{path}"], capture_output=True, text=True)
    return r.stdout.splitlines() if r.returncode == 0 else []


def count(ref, paths):
    allv = code = 0
    for p in paths:
        for l in lines(ref, p):
            s = l.strip()
            if not s:
                continue
            allv += 1
            if not (s.startswith("//") or s.startswith("/*") or s.startswith("*")):
                code += 1
    return allv, code


def isr_vars(ref, path):
    return len([l for l in lines(ref, path) if re.match(r"\s*static\s+volatile\s", l)])


def main():
    out = []
    a3, c3 = count("lab3", LR3)
    a4, c4 = count("wt", LR4)
    out.append("| Показник | Super loop (ЛР3, тег lab3) | FreeRTOS (ЛР4) |")
    out.append("|---|---|---|")
    out.append(f"| Прикладний шар: оркестрація продукту (product + планувальник / product_rtos + rtos_port), рядків усього | {a3} | {a4} |")
    out.append(f"| те саме, рядків коду без коментарних | {c3} | {c4} |")
    for k, v in EXTRA4.items():
        a, c = count("wt", v)
        out.append(f"| додатково в ЛР4: {k}, рядків усього / коду | — | {a} / {c} |")
    t3 = sum(count("lab3", subprocess.run(["git", "ls-tree", "-r", "--name-only", "lab3", d], capture_output=True, text=True).stdout.split())[0] for d in ["firmware/App", "firmware/Core/Src", "firmware/Platform", "firmware/Devices", "firmware/Common"])
    import glob
    fs = [f for d in ["firmware/App", "firmware/Core/Src", "firmware/Core/Inc", "firmware/Platform", "firmware/Devices", "firmware/Common"] for f in glob.glob(d + "/**/*.[ch]", recursive=True)]
    t4 = count("wt", fs)[0]
    out.append(f"| Власний код прошивки загалом (App, Core, Platform, Devices, Common), рядків | {t3} | {t4} |")
    out.append("| Глобальні змінні, які пише ISR і читає цикл/задача (підрахунок вручну за platform_perf.c і product_rtos.c) | 5 (`btn_cyc`, `btn_pending`, `btn_overrun`, `eol_cyc`, кільце `rx_ring`) | 3 (`eol_cyc`, лічильники `btn_lost`, `rx_lost`; решта йде через чергу `q_btn` і stream buffer `sb_rx`) |")
    out.append("| Місць у коді, які правляться при додаванні нової періодичної задачі | 2 (функція, рядок `TASK_DEF`) | 5 (функція, рядок `SPEC`, три масиви розміру 12: `rt[]`, `work_of[]`, `c[]` у `product_print_stats`) |")
    print("\n".join(out))


if __name__ == "__main__":
    main()
