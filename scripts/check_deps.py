#!/usr/bin/env python3
"""Перевірка правила залежностей між шарами прошивки (запускається в `make check-deps` і в CI).

Шари та дозволені #include (стандартні <...> заголовки дозволені скрізь):
  Devices   -> лише власні заголовки Devices/Inc (bus.h тощо)           [нічого про HAL і плату]
  App       -> Devices/Inc, App/Inc, Platform/Inc/platform.h             [без HAL, main.h, board.h]
  Platform  -> усе, крім App (HAL + board.h + Devices/bus.h)
  Core      -> усе
Додатково: у Devices/ та App/ заборонені ідентифікатори HAL_/GPIO/hi2c/hspi/huart як текст.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent / "firmware"
INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)

def headers(layer):
    return {p.name for p in (ROOT / layer / "Inc").glob("*.h")}

OWN = {l: headers(l) for l in ("Devices", "App", "Platform", "Core")}
ALLOWED = {
    "Devices": OWN["Devices"],
    "App": OWN["Devices"] | OWN["App"] | {"platform.h"},
    "Platform": OWN["Devices"] | OWN["Platform"] | OWN["Core"],
}
FORBIDDEN_TEXT = re.compile(r'\b(HAL_\w+|hi2c\d|hspi\d|huart\d|hadc\d|GPIO[A-K]\b|TIM\d\b)')

errors = []
for layer in ("Devices", "App", "Platform"):
    for f in sorted((ROOT / layer).rglob("*.[ch]")):
        text = f.read_text(encoding="utf-8")
        for inc in INC.findall(text):
            name = pathlib.Path(inc).name
            if layer == "Platform" and name.startswith("stm32f4xx"):
                continue                                  # HAL/CMSIS — законно лише для Platform (і Core)
            if name not in ALLOWED[layer]:
                errors.append(f"{f.relative_to(ROOT)}: шар {layer} не має права включати \"{inc}\"")
        if layer in ("Devices", "App"):
            code = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)   # коментарі не рахуємо
            for m in FORBIDDEN_TEXT.finditer(code):
                errors.append(f"{f.relative_to(ROOT)}: шар {layer} використовує апаратно-залежний символ {m.group(0)}")

if errors:
    print("Порушення правила залежностей:")
    print("\n".join("  " + e for e in errors))
    sys.exit(1)
print("Залежності між шарами в порядку (Devices <- App <- Core; Platform -> HAL).")
