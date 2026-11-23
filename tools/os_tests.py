#!/usr/bin/env python3
"""Сценарії ЛР4, п. 7.2 (навмисні поломки) і вартість ОС (п. 6.5) на прошивці FreeRTOS. Пише docs/metrics/lr4_ostests_<час>.log.

  python3 tools/os_tests.py            # усі, крім переповнення стека (TEST STK перезапускає плату: --stk)
  python3 tools/os_tests.py --stk      # лише переповнення стека
Кожна команда друкується в лог разом із відповіддю прошивки (рядки [test], STAT, TASK, OSCOST).
"""
import argparse
import datetime
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab_serial import Board  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "metrics")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stk", action="store_true")
    ap.add_argument("--tag", default="lr4_ostests")
    a = ap.parse_args()
    path = os.path.join(OUT, f"{a.tag}_{datetime.datetime.now():%Y%m%d_%H%M%S}.log")
    log = open(path, "w", encoding="utf-8")
    b = Board(log=None)

    def run(cmd, wait=3.0, keep=("[test]", "STAT", "TASK", "OSCOST", "HEAP", "LOAD", "STACK", "[os]", "FATAL", "VER", "MEM", "[cli]")):
        log.write(f"$ {cmd}\n")
        lines, _ = b.send(cmd, wait=wait, retries=2)
        for ln in lines:
            if ln.startswith(keep):
                log.write(ln + "\n")
        log.flush()
        return lines

    log.write("# " + b.version() + "\n")
    if a.stk:
        run("TEST STK", wait=8.0)
        time.sleep(3)
        log.write("# після TEST STK: відповідь на VER\n")
        run("VER", wait=3.0)
        log.close(); print(path); return
    # 1. інверсія пріоритетів: м'ютекс (успадкування) проти двійкового семафора
    for mode in ("", "SEM"):
        for _ in range(3):
            run(f"TEST INV {mode}".strip(), wait=3.5)
    # 2. голодування: задача P7 без блокування 1500 мс; що бачать решта і run-time stats
    run("STATS RESET", wait=1.5)
    run("TEST STARVE 1500", wait=5.0)
    run("STATS", wait=3.0)
    run("TASKS", wait=3.0)
    # 3. вичерпання черги: споживач 200 елем./с, виробник без паузи
    run("TEST QFLOOD 200 0", wait=6.0)
    run("TEST QFLOOD 200 50", wait=9.0)
    # 4. вичерпання heap ОС
    run("HEAP", wait=2.0)
    run("TEST HEAP", wait=3.0)
    run("HEAP", wait=2.0)
    # 5. вартість ОС: перемикання контексту, тік
    run("TEST CTX 2000", wait=6.0)
    run("STATS RESET", wait=1.5)
    time.sleep(20)
    run("OSCOST", wait=2.5)
    run("TASKS", wait=3.0)
    # 6. захоплення кадру з увімкненими перериваннями між рядками
    run("TEST CAMDIAG 4000", wait=7.0)
    run("TEST CAMDIAG 4000", wait=7.0)
    log.close()
    print(path)


if __name__ == "__main__":
    main()
