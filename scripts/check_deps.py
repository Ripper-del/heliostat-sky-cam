#!/usr/bin/env python3
"""Перевірка правила залежностей між шарами прошивки (запускається в `make check-deps` і в CI).

Шари та дозволені #include (стандартні <...> заголовки дозволені скрізь):
  Common    -> лише власні заголовки Common/Inc (беззалізні примітиви: кільцевий буфер)
  Devices   -> Common/Inc + власні заголовки Devices/Inc (bus.h тощо)  [нічого про HAL і плату]
  App       -> Common/Inc, Devices/Inc, App/Inc, Platform/Inc/platform.h, qpc.h (ЛР5) [без HAL, main.h, board.h]
  Platform  -> усе, крім App (HAL + board.h + Devices/bus.h + Common/Inc)
  Core      -> усе
Додатково: у Devices/ та App/ заборонені ідентифікатори HAL_/GPIO/hi2c/hspi/huart як текст.
ЛР5 (п. 4.4 методички): у App/ заборонені API синхронізації і затримок FreeRTOS — AO працюють лише через QP.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent / "firmware"
INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)

def headers(layer):
    return {p.name for p in (ROOT / layer / "Inc").glob("*.h")}

OWN = {l: headers(l) for l in ("Common", "Devices", "App", "Platform", "Core")}
ALLOWED = {
    "Common": OWN["Common"],
    "Devices": OWN["Common"] | OWN["Devices"],
    "App": OWN["Common"] | OWN["Devices"] | OWN["App"] | {"platform.h", "qpc.h"},
    "Platform": OWN["Common"] | OWN["Devices"] | OWN["Platform"] | OWN["Core"],
}
FORBIDDEN_TEXT = re.compile(r'\b(HAL_\w+|hi2c\d|hspi\d|huart\d|hadc\d|GPIO[A-K]\b|TIM\d\b)')
RTOS_TEXT = re.compile(r'\b(xSemaphore\w*|xQueue\w*|vTaskDelay\w*|\w*TaskNotify\w*|\w*EventGroup\w*|xTimer\w*|xStreamBuffer\w*|taskENTER_CRITICAL|vTaskSuspendAll)\b')

errors = []
for layer in ("Common", "Devices", "App", "Platform"):
    for f in sorted((ROOT / layer).rglob("*.[ch]")):
        text = f.read_text(encoding="utf-8")
        for inc in INC.findall(text):
            name = pathlib.Path(inc).name
            if layer == "Platform" and name.startswith("stm32f4xx"):
                continue                                  # HAL/CMSIS — законно лише для Platform (і Core)
            if name not in ALLOWED[layer]:
                errors.append(f"{f.relative_to(ROOT)}: шар {layer} не має права включати \"{inc}\"")
        if layer in ("Common", "Devices", "App"):
            code = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)   # коментарі не рахуємо
            for m in FORBIDDEN_TEXT.finditer(code):
                errors.append(f"{f.relative_to(ROOT)}: шар {layer} використовує апаратно-залежний символ {m.group(0)}")
        if layer == "App":
            for m in RTOS_TEXT.finditer(code):
                errors.append(f"{f.relative_to(ROOT)}: AO не можуть блокуватись або синхронізуватись через FreeRTOS: {m.group(0)}")

if errors:
    print("Порушення правила залежностей:")
    print("\n".join("  " + e for e in errors))
    sys.exit(1)
print("Layer dependencies OK (Devices <- App <- Core; Platform -> HAL).")
