#!/usr/bin/env python3
"""ЛР5: перевірка, що діаграми станів (docs/statecharts/*.puml) збігаються з кодом AO (firmware/App/Src/ao_*.c).
  - кожен стан на діаграмі є функцією-обробником <Ao>_<стан> у коді, і навпаки;
  - кожен сигнал на діаграмі є в enum Signals (firmware/App/Inc/ao.h), і кожен сигнал, який обробляє AO, є на діаграмі.
  python3 tools/check_statecharts.py
"""
import glob, os, re, sys
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SC, SRC = os.path.join(R, "docs", "statecharts"), os.path.join(R, "firmware", "App", "Src")
sigs = set(re.findall(r"\b([A-Z_]+)_SIG\b", open(os.path.join(R, "firmware", "App", "Inc", "ao.h"), encoding="utf-8").read()))
code = "".join(open(p, encoding="utf-8").read() for p in glob.glob(os.path.join(SRC, "ao_*.c")))
errors = []
AO = {"log": "Log", "snapshot": "Snapshot", "camera": "Camera", "servo": "Servo", "imu": "Imu"}
FLAT = {"button": ("Button", "ready"), "adc": ("Adc", "sampling"), "control": ("Control", "running"), "cli": ("Cli", "ready"),
        "tick": ("Tick", "reporting"), "loss": ("Loss", "watching"), "led": ("Led", "blinking")}
def handled(prefix, states):
    """сигнали в case ...: обробників цього AO"""
    out = set()
    for st in states:
        m = re.search(r"static QState %s_%s\([^)]*\)\n\{(.*?)\n\}\n" % (prefix, st), code, re.S)
        if m: out |= set(re.findall(r"case ([A-Z_]+)_SIG", m.group(1)))
    return out - {"Q_ENTRY", "Q_EXIT", "Q_INIT"}
for name, prefix in AO.items():
    t = open(os.path.join(SC, name + ".puml"), encoding="utf-8").read()
    dia = set(re.findall(r"^\s*state (\w+)", t, re.M)) | set(re.findall(r"^\s*(\w+) :", t, re.M))
    dia -= {"title", "skinparam"}
    cod = set(re.findall(r"static QState %s_(\w+)\(%s \*me, QEvt" % (prefix, prefix), code))
    for s in sorted(dia - cod): errors.append(f"{name}: стан '{s}' є на діаграмі, але немає {prefix}_{s}() у коді")
    for s in sorted(cod - dia): errors.append(f"{name}: функція {prefix}_{s}() є в коді, але стану немає на діаграмі")
    used = set(re.findall(r"\b([A-Z][A-Z_]{2,})\b", t)) & (sigs | {s for s in re.findall(r"\b([A-Z][A-Z_]+)\b", t)})
    for s in sorted({u for u in used if u in sigs or u.endswith("_REQ") or u.endswith("_DONE")} - sigs):
        errors.append(f"{name}: сигнал '{s}' на діаграмі не існує в ao.h")
    for s in sorted(handled(prefix, cod) - set(re.findall(r"\b([A-Z_]+)\b", t))):
        errors.append(f"{name}: AO обробляє {s}, але на діаграмі його немає")
flat = open(os.path.join(SC, "flat.puml"), encoding="utf-8").read()
for name, (prefix, st) in FLAT.items():
    if not re.search(r"static QState %s_%s\(" % (prefix, st), code): errors.append(f"flat: немає {prefix}_{st}() у коді")
    if f'"{name} : {st}"' not in flat: errors.append(f"flat: немає стану '{name} : {st}' на діаграмі")
    for s in sorted(handled(prefix, [st]) - set(re.findall(r"\b([A-Z_]+)\b", flat))):
        errors.append(f"flat/{name}: AO обробляє {s}, але на діаграмі його немає")
if errors:
    print("\n".join(errors)); sys.exit(1)
print(f"Statecharts match code: {len(AO)} machines with states + {len(FLAT)} flat AO, {len(sigs)} signals in ao.h")
