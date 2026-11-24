#!/usr/bin/env python3
"""Прошивка NUCLEO через диск ST-Link (NOD_F411RE): копіюються ЛИШЕ байти .bin, без атрибутів macOS.

  python3 tools/flash.py firmware/build/firmware.bin

Чому не `cp`: macOS при копіюванні файлу з розширеними атрибутами (com.apple.provenance, quarantine) намагається
записати на FAT-диск ST-Link службовий файл AppleDouble `._<ім'я>`. Після такого запису ST-Link переставав приймати
дані від комп'ютера в канал VCP (запис у порт блокувався, команди не доходили), і допомагало лише вимкнення USB.
Див. docs/problems.md, проблема ЛР4 «ST-Link VCP перестає приймати команди».
"""
import os
import sys
import time

VOL = os.environ.get("NUCLEO_VOL", "/Volumes/NOD_F411RE")


def flash(path, wait=20.0):
    if not os.path.isdir(VOL):
        sys.exit(f"диск {VOL} не знайдено (плата підключена?)")
    data = open(path, "rb").read()
    dst = os.path.join(VOL, os.path.basename(path))
    with open(dst, "wb") as f:              # лише дані: без метаданих, без AppleDouble
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    print(f"записано {len(data)} Б у {dst}; чекаю перезапуск плати {wait:.0f} с")
    time.sleep(wait)


if __name__ == "__main__":
    flash(sys.argv[1] if len(sys.argv) > 1 else "firmware/build/firmware.bin")
