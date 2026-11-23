#!/bin/bash
# ЛР4: сценарії п. 7.2 і варіанти збірки після основної матриці (tools/measure_all.py --fw lr4). Запускати з кореня репозиторію, плата підключена.
#   BIN=/шлях/до/основної/збірки.bin tools/lr4_post.sh
# Варіанти: частота тіка 100 Гц (S3 x2), вимкнений контроль переповнення стека (TEST STK), heap 12 і 8 КБ (завантаження), основна збірка.
set -u
PY=${PY:-$HOME/heliostat-venv/bin/python}
BIN=${BIN:?вкажіть BIN=основна збірка .bin}
WORK=${WORK:-/tmp/lr4_variants}
mkdir -p "$WORK"
flash() { $PY tools/flash.py "$1"; }   # не cp: див. tools/flash.py
build_variant() {   # $1 = назва, $2 = RTOS_FLAGS
  rm -rf firmware/build
  make build RTOS_FLAGS="$2" >/dev/null 2>&1 && cp firmware/build/firmware.bin "$WORK/$1.bin" && echo "зібрано $1 ($2)"
}
# 1. основна збірка: хронологія S3, перевірка FR, сценарії ОС, режим камери 0
flash "$BIN"
$PY tools/run_scenario.py trace --duration 3 --tag lr4_trace
$PY tools/fr_check_lr4.py
$PY tools/os_tests.py
$PY scripts/sendcmd.py "CAMIRQ 0" --wait 2 >/dev/null
$PY tools/run_scenario.py s3 --duration 60 --btnsim 700 --tag lr4_camirq0_s3a
$PY scripts/sendcmd.py "CAMIRQ 1" --wait 2 >/dev/null
$PY tools/os_tests.py --stk --tag lr4_stk_check
# 2. частота тіка 100 Гц
build_variant tick100 "-DRTOS_TICK_HZ=100"
flash "$WORK/tick100.bin"
$PY tools/run_scenario.py s3 --duration 60 --btnsim 700 --tag lr4_tick100_s3a
$PY tools/run_scenario.py s3 --duration 60 --btnsim 700 --tag lr4_tick100_s3b
$PY tools/run_scenario.py s2 --duration 60 --tag lr4_tick100_s2a
# 3. без контролю переповнення стека
build_variant nosof "-DRTOS_NO_SOF"
flash "$WORK/nosof.bin"
$PY tools/os_tests.py --stk --tag lr4_stk_nocheck
# 4. heap ОС: 12 КБ (має вистачити) і 8 КБ (замалий для створення всіх задач: хук malloc і configASSERT)
for kb in 12 8; do
build_variant heap${kb}k "-DRTOS_HEAP_BYTES=$((kb * 1024))"
$PY - <<PYEOF
import sys, datetime
sys.path.insert(0, "tools")
import measure_all as M
lines = M.flash_and_capture("$WORK/heap${kb}k.bin", wait=25)
open("docs/metrics/lr4_heap${kb}k_boot_%s.log" % datetime.datetime.now().strftime("%Y%m%d_%H%M%S"), "w", encoding="utf-8").write("\n".join(lines) + "\n")
print("heap${kb}k boot: %d рядків, останній: %s" % (len(lines), lines[-1] if lines else "-"))
PYEOF
done
# 5. повернення основної збірки
rm -rf firmware/build
flash "$BIN"
echo "post: готово"
