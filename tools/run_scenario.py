#!/usr/bin/env python3
"""Запуск сценаріїв ЛР3 на платі і запис логу в docs/metrics/.

  python3 tools/run_scenario.py s1 --duration 60          # простій: лише періодичні задачі (--no-camera: без захоплення кадру)
  python3 tools/run_scenario.py s2 --duration 60          # номінальний: рідкісні команди
  python3 tools/run_scenario.py s3 --duration 60 --servo  # найгірший: довга операція + потік команд (+ кнопка)
  python3 tools/run_scenario.py break_adc                 # поступово підвищуємо частоту опитування ADC
  python3 tools/run_scenario.py break_cam                 # поступово підвищуємо частоту знімків
  python3 tools/run_scenario.py long --duration 3600      # тривала робота в режимі S2
  python3 tools/run_scenario.py fail                      # відмова пристрою (відключити MPU-6050 за підказкою)

Перший рядок логу — версія прошивки (git-хеш), далі сирі рядки плати, у кінці блок STATS. Порт: перший /dev/cu.usbmodem*.
"""
import argparse
import datetime
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab_serial import Board, parse_load, parse_stat_lines  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "metrics")


def open_log(name):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, f"{name}_{datetime.datetime.now():%Y%m%d_%H%M%S}.log")
    return open(path, "w", encoding="utf-8"), os.path.relpath(path)


def header(b, log, scenario, args):
    log.write(f"# scenario={scenario} duration={getattr(args, 'duration', 0)} servo={int(getattr(args, 'servo', False))} btnsim_ms={getattr(args, 'btnsim', 0) or 0}\n")
    ver = b.version()
    b.is_os = "os=FreeRTOS" in ver                      # ЛР4: прошивка на FreeRTOS додатково звітує TASKS/STACK/HEAP/OSCOST
    log.write("# " + ver + "\n")


def finish(b, log, tag="final"):
    stats, load, lines = b.stats()
    mem = b.mem()
    log.write(f"# --- STATS {tag} ---\n")
    for ln in lines:
        log.write(ln + "\n")
    log.write(f"# MEM {mem}\n")
    if getattr(b, "is_os", False):
        for cmd, tag in (("TASKS", "TASK"), ("STACK", "STACK"), ("HEAP", "HEAP"), ("OSCOST", "OSCOST")):
            lines, _ = b.send(cmd, wait=2.0)
            for ln in lines:
                if ln.startswith(tag):
                    log.write(ln + "\n")
    log.flush()
    return stats, load


def s1(b, log, a):
    """Простій. З --no-camera камеру на час прогону вимикаємо (PERIOD camera 600000): "чисті" періодичні задачі без довгої
    операції — це точка відліку, щоб бачити, скільки коштує захоплення кадру."""
    b.send("SERVO OFF")
    if a.no_camera:
        b.send("PERIOD camera 600000")
    b.send("STATS RESET")
    b.read_lines(a.duration)
    finish(b, log)
    if a.no_camera:
        b.send("PERIOD camera 3000")


def s2(b, log, a):
    b.send("SERVO OFF"); b.send("STATS RESET")
    t0 = time.time()
    n = 0
    while time.time() - t0 < a.duration:
        b.read_lines(10)
        b.send("MODE" if n % 3 else "LOG 3", wait=1.0)
        n += 1
    finish(b, log)


def s3(b, log, a):
    """Найгірший: знімок у flash кожні ~3,2 с (захоплення + стирання 5 секторів + запис 19 КБ), потік команд MODE
    ~20 Гц, подія-кнопка (якщо --button: просимо людину натискати B1)."""
    b.send("SERVO ON" if a.servo else "SERVO OFF"); b.send("STATS RESET")
    stats0, load0, _ = b.stats()
    sent = answered = 0
    resp = []
    t0 = time.time(); next_snap = t0
    if a.button:
        print(">>> Натискайте B1 приблизно раз на секунду протягом сценарію.")
    if a.btnsim:
        b.send(f"BTNSIM {a.btnsim}")                     # програмні події кнопки (TIM2 -> EXTI): повторювані, без людини
    while time.time() - t0 < a.duration:
        now = time.time()
        if now >= next_snap:
            b.send_raw(b"SNAP\r")
            next_snap = now + 3.2
        b.send_raw(b"\rMODE\r")
        sent += 1
        t = time.time()
        got = [l for l in b.read_lines(0.05) if l.startswith("[prod] mode")]
        if got:
            answered += 1
            resp.append(time.time() - t)
    b.read_lines(2.0)
    stats, load, _ = b.stats()
    if a.btnsim:
        b.send("BTNSIM 0")
    log.write(f"# S3 commands sent={sent} answered_in_50ms={answered}\n")
    log.write(f"# S3 uart_rx_overflow_delta={load.get('uart_rx_overflow', 0) - load0.get('uart_rx_overflow', 0)}\n")
    finish(b, log)
    b.send("SERVO OFF")


def break_series(b, log, a, task, values_ms, wait_s):
    b.send("SERVO OFF")
    rows = []
    try:
        for ms in values_ms:
            b.send(f"PERIOD {task} {ms}"); b.send("STATS RESET")
            b.read_lines(wait_s)
            stats, load, lines = b.stats()
            st = stats.get(task, {})
            adc = stats.get("adc", {})
            rows.append((ms, st.get("n", 0), st.get("missed", 0), st.get("late_p99_us", 0), st.get("late_max_us", 0),
                         adc.get("missed", 0), stats.get("loop", {}).get("max_us", 0), load.get("pct", 0)))
            log.write(f"# POINT period_ms={ms} {task}_n={rows[-1][1]} {task}_missed={rows[-1][2]} {task}_late_p99_us={rows[-1][3]} "
                      f"{task}_late_max_us={rows[-1][4]} adc_missed={rows[-1][5]} loop_max_us={rows[-1][6]} load_pct={rows[-1][7]}\n")
            for ln in lines:
                log.write(ln + "\n")
            log.flush()
    finally:
        defaults = {"adc": 10, "camera": 3000}
        b.send(f"PERIOD {task} {defaults.get(task, 100)}")


def break_adc(b, log, a):
    break_series(b, log, a, "adc", [10, 5, 3, 2, 1], 10)


def break_cam(b, log, a):
    break_series(b, log, a, "camera", [3000, 2000, 1500, 1000], 20)


def long_run(b, log, a):
    b.send("SERVO OFF"); b.send("STATS RESET")
    t0 = time.time(); k = 0
    while time.time() - t0 < a.duration:
        b.read_lines(min(300, max(1, a.duration - (time.time() - t0))))
        k += 1
        stats, load = finish(b, log, f"t={int(time.time() - t0)}s")
        lines, _ = b.send("LOG 1", wait=1.5)
        for ln in lines:
            log.write(ln + "\n")


def fail(b, log, a):
    b.send("STATS RESET")
    s0, l0, _ = b.stats()
    input(">>> Відключіть датчик MPU-6050 (дроти SDA/SCL), натисніть Enter...")
    b.read_lines(15)
    s1_, l1, lines = b.stats()
    log.write("# FAIL disconnected: imu_err=%s imu_max_us=%s\n" % (l1.get("imu_err"), s1_.get("imu", {}).get("max_us")))
    for ln in lines:
        log.write(ln + "\n")
    input(">>> Підключіть датчик назад, натисніть Enter...")
    b.send("STATS RESET")
    b.read_lines(15)
    finish(b, log, "after_reconnect")


def trace(b, log, a):
    """ЛР4: хронологія перемикань задач у фрагменті S3 — кнопка (BTNSIM 700) під час знімка у flash. Лише для прошивки на FreeRTOS."""
    b.send("SERVO OFF"); b.send("STATS RESET")
    b.send("BTNSIM 700")
    b.read_lines(1.0)
    b.send("TRACE ARM")
    b.send_raw(b"\rSNAP\r")
    b.read_lines(a.duration)
    b.send("BTNSIM 0")
    lines, _ = b.send("TRACE DUMP", wait=14.0)
    for ln in lines:
        if ln.startswith("TRACE") or ln.startswith("[trace]"):
            log.write(ln + "\n")
    finish(b, log, "trace")


def sda(b, log, a):
    """Відмова шини: SDA I2C1 замкнено на землю програмно (FAULT SDA 1) на a.duration с, потім відпущено. Те саме, що дріт на GND у ЛР3."""
    b.send("SERVO OFF"); b.send("STATS RESET")
    b.send("FAULT SDA 1")
    b.read_lines(a.duration)
    stats, load, lines = b.stats()
    log.write("# FAULT SDA on: imu_err=%s imu_max_us=%s adc_late_max_us=%s\n" % (load.get("imu_err"), stats.get("imu", {}).get("max_us"), stats.get("adc", {}).get("late_max_us")))
    for ln in lines:
        log.write(ln + "\n")
    b.send("FAULT SDA 0"); b.send("STATS RESET")
    b.read_lines(10)
    finish(b, log, "after_release")


SCEN = {"s1": s1, "s2": s2, "s3": s3, "break_adc": break_adc, "break_cam": break_cam, "long": long_run, "fail": fail, "sda": sda, "trace": trace}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario", choices=sorted(SCEN))
    ap.add_argument("--duration", type=int, default=60, help="секунд (s1, s2, s3), для long — загальна тривалість")
    ap.add_argument("--servo", action="store_true", help="s3: ввімкнути серво")
    ap.add_argument("--no-camera", action="store_true", help="s1: вимкнути періодичне захоплення кадру на час прогону")
    ap.add_argument("--button", action="store_true", help="s3: просити людину натискати B1")
    ap.add_argument("--btnsim", type=int, default=0, metavar="MS", help="s3: програмні події кнопки кожні MS мс (BTNSIM), без людини")
    ap.add_argument("--tag", help="префікс імені лог-файлу (за замовч. назва сценарію)")
    ap.add_argument("--port")
    a = ap.parse_args()
    log, path = open_log(a.tag or a.scenario)
    b = Board(a.port, log=log)
    header(b, log, a.scenario, a)
    print(f"лог: {path}")
    SCEN[a.scenario](b, log, a)
    log.close()
    print("готово:", path)


if __name__ == "__main__":
    main()
