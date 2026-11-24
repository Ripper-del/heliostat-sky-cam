#!/usr/bin/env python3
"""Повна матриця вимірювань ЛР3/ЛР4 для однієї прошивки (однаковий метод для обох, ЛР4 п. 7.1).

  python3 tools/measure_all.py --fw lr3 --bin /шлях/lr3.bin     # S1, S2 x3, S3 x3, пошук точки зламу, SDA, старт x3
  python3 tools/measure_all.py --fw lr4 --bin /шлях/lr4.bin

Прошивка заливається копіюванням .bin на диск NOD_F411RE. Логи: docs/metrics/<fw>_<сценарій>_<час>.log.
S3: знімок у flash кожні 3,2 с + потік команд MODE ~20 Гц + програмні події кнопки кожні 700 мс (BTNSIM), серво вимкнені.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PY = sys.executable
VOL = "/Volumes/NOD_F411RE"


def ports():
    return sorted(glob.glob("/dev/cu.usbmodem*"))


def flash_and_capture(binpath, wait=30):
    """Копіює .bin на NOD_F411RE і збирає завантажувальний лог (потрібен рядок boot_ms). Повертає список рядків."""
    import serial
    with open(os.path.join(VOL, os.path.basename(binpath)), "wb") as f:   # лише байти, без атрибутів macOS (tools/flash.py)
        f.write(open(binpath, "rb").read()); f.flush(); os.fsync(f.fileno())
    t0 = time.time(); lines = []; s = None; buf = b""
    while time.time() - t0 < wait:
        try:
            if s is None:
                p = ports()
                if p:
                    s = serial.Serial(p[0], 115200, timeout=0.05)
            if s is not None:
                d = s.read(4096)
                if d:
                    buf += d
                    *done, buf = buf.split(b"\n")
                    lines += [x.decode(errors="replace").strip("\r") for x in done if x.strip()]
                    if any("[cli] type" in l for l in lines):
                        break
        except Exception:
            s = None
            time.sleep(0.2)
    if s is not None:
        s.close()
    return lines


def run(args, tag):
    cmd = [PY, os.path.join(ROOT, "tools", "run_scenario.py")] + args + ["--tag", tag]
    print(">>", " ".join(cmd[2:]), flush=True)
    subprocess.run(cmd, check=False, cwd=ROOT)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fw", required=True, help="префікс логів: lr3 або lr4")
    ap.add_argument("--bin", required=True)
    ap.add_argument("--dur", type=int, default=60)
    ap.add_argument("--skip-flash", action="store_true")
    ap.add_argument("--quick", action="store_true", help="лише S3 x1 (перевірка)")
    a = ap.parse_args()
    fw, d = a.fw, str(a.dur)
    out = os.path.join(ROOT, "docs", "metrics")
    if not a.skip_flash:
        boots = []
        for i in range(3):                                   # старт: час від скидання до першого виміру (boot_ms), 3 повтори
            lines = flash_and_capture(a.bin)
            boots.append(lines)
            time.sleep(2)
        with open(os.path.join(out, f"{fw}_boot_{time.strftime('%Y%m%d_%H%M%S')}.log"), "w", encoding="utf-8") as f:
            for i, ls in enumerate(boots, 1):
                f.write(f"# boot {i}\n")
                f.write("\n".join(ls) + "\n")
        time.sleep(3)
    if a.quick:
        run(["s3", "--duration", d, "--btnsim", "700"], f"{fw}_s3q")
        return
    run(["s1", "--duration", d, "--no-camera"], f"{fw}_s1nocam")
    run(["s1", "--duration", d], f"{fw}_s1")
    for k in "abc":
        run(["s2", "--duration", d], f"{fw}_s2{k}")
    for k in "abc":
        run(["s3", "--duration", d, "--btnsim", "700"], f"{fw}_s3{k}")
    run(["break_adc"], f"{fw}_break_adc")
    run(["break_cam"], f"{fw}_break_cam")
    run(["sda", "--duration", "20"], f"{fw}_sda")


if __name__ == "__main__":
    main()
