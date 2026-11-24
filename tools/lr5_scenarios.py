#!/usr/bin/env python3
"""ЛР5: сценарії п. 9.2–9.3 методички на прошивці QP/C (команди TEST у прошивці, Core/Src/qp_tests.c).

  python3 tools/lr5_scenarios.py <сценарій> [--tag префікс] [--duration 30]

Сценарії на основній збірці:
  lograte   S3 + запис журналу кожні 50 мс (база для порівняння очікування flash)
  inv       те саме + imu (середній пріоритет) займає процесор 50 мс кожні 100 мс (інверсія, п. 9.2)
  block     S3 + LOGRATE + log чекає flash блокуванням в обробнику (п. 9.3.2)
  owner5    S3 + LOGRATE + власник flash тримає процесор 5 мс у кожному кроці (п. 9.3.2)
  ctrl30    S3 + control робить 30 мс роботи на кожному SAMPLE (довгий крок без блокування, п. 9.3.3)
  onestep   S3 + LOGRATE + кадр пишеться одним кроком (п. 9.3.3)
  starve    button шле сам собі подію 2 с (голодування, п. 9.2)
  snapto    таймаут знімка 1000 мс, коротший за знімок (неочікувана подія, п. 9.3.6)
  flashfail імітований збій flash 3 с і відновлення (збій шини, п. 9.3.8)
  qflood    50 команд без пауз (переповнення черги cli, п. 9.3.4)
  pool      пул вичерпується без повернення: Q_NEW -> Q_onError (п. 9.3.5); плата перезапускається
  boot      лише стартовий лог 15 с (для збірок «малий стек» і т. п.)
Для збірки LR5_FLASH_MUTEX ті самі lograte / inv з іншим --tag.
"""
import argparse
import os
import sys
import time
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab_serial import Board  # noqa: E402
from run_scenario import finish, header, open_log, s3  # noqa: E402


def s3_with(b, log, dur, *tests):
    for t in tests:
        b.send("TEST " + t)
    s3(b, log, types.SimpleNamespace(duration=dur, servo=False, button=False, btnsim=700))
    b.send("TEST STATUS")
    b.send("TEST OFF")


def run(name, b, log, a):
    d = a.duration
    if name == "lograte":   s3_with(b, log, d, "LOGRATE 50")
    elif name == "inv":     s3_with(b, log, d, "LOGRATE 50", "HOG 50")
    elif name == "block":   s3_with(b, log, d, "LOGRATE 50", "BLOCK 1")
    elif name == "owner5":  s3_with(b, log, d, "LOGRATE 50", "OWNER 5")
    elif name == "ctrl30":  s3_with(b, log, d, "CTRL 30")
    elif name == "onestep": s3_with(b, log, d, "LOGRATE 50", "ONESTEP 1")
    elif name == "starve":
        b.send("SERVO OFF"); b.send("STATS RESET")
        b.send("TEST STARVE 2000", wait=1.0)
        b.read_lines(4)
        finish(b, log)
    elif name == "snapto":
        b.send("STATS RESET"); b.send("TEST SNAPTO 1000")
        for _ in range(3):
            b.send_raw(b"\rSNAP\r"); b.read_lines(4.5)
        b.send("TEST STATUS")
        finish(b, log); b.send("TEST OFF")
    elif name == "flashfail":
        b.send("STATS RESET"); b.send("TEST LOGRATE 100")
        b.send("TEST FLASHFAIL 1"); t0 = time.time()
        b.read_lines(3)
        b.send("TEST FLASHFAIL 0"); log.write(f"# flash fail on for {time.time() - t0:.1f} s\n")
        b.read_lines(3)
        b.send("LOG 2")
        finish(b, log); b.send("TEST OFF")
    elif name == "qflood":
        b.send("STATS RESET")
        b.send_raw(b"".join(b"MODE\r" for _ in range(50)))     # 50 команд одним шматком, без пауз
        lines = b.read_lines(8)
        log.write(f"# qflood: answers={sum(1 for l in lines if l.startswith('[prod] mode'))} of 50\n")
        finish(b, log)
    elif name == "pool":
        b.send_raw(b"\rTEST POOL\r")
        b.read_lines(12)                                       # FATAL Q_onError ... і новий старт
    elif name == "boot":
        b.read_lines(15)
    else:
        raise SystemExit("невідомий сценарій " + name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario")
    ap.add_argument("--tag")
    ap.add_argument("--duration", type=int, default=30)
    ap.add_argument("--port")
    a = ap.parse_args()
    log, path = open_log(a.tag or ("lr5_" + a.scenario))
    b = Board(a.port, log=log)
    if a.scenario not in ("pool", "boot"):
        header(b, log, a.scenario, a)
    print("лог:", path, flush=True)
    run(a.scenario, b, log, a)
    log.close()
    print("готово:", path)


if __name__ == "__main__":
    main()
