# Геліостат з камерою класифікації неба — ЛР1 bring-up, ЛР2 інтеграція сенсорів, ЛР3 super loop, ЛР4 FreeRTOS, ЛР5 QP/C

Навчальний проєкт з курсу «Системне програмування вбудованих систем».
Платформа: NUCLEO-F411RE / F446RE, STM32 HAL, C, збірка в Docker.

Команда: Андрій Плешу, Тимур Варшавський, група ІО-36.

## Звіти

| Робота | Звіт | Відео |
|---|---|---|
| ЛР1 bring-up | звіт здано у Word | — |
| ЛР2 інтеграція сенсорів | `docs/lab2-report.md` | https://drive.google.com/file/d/1bkmDj3VoO9egDjwT98JWo7B4difG5Dfp/view?usp=sharing |
| ЛР3 super loop | `docs/lab3-report.md` | https://drive.google.com/drive/folders/1hXphGatK6j-2GWOnMWaTJKAtgvG1KWl8?usp=sharing |
| ЛР4 FreeRTOS | `docs/lab4-report.md`, порівняння — `docs/comparison-lr3-lr4.md` | https://drive.google.com/drive/folders/1hXphGatK6j-2GWOnMWaTJKAtgvG1KWl8?usp=sharing |
| ЛР5 QP/C на FreeRTOS | `docs/lab5-report.md`, дизайн AO — `docs/qp-design.md`, порівняння трьох підходів — `docs/comparison.md` | буде додано |

## Що це за проєкт

Квадрант із 4 фоторезисторів GM5528 визначає напрямок на джерело світла, два серводвигуни
(азимут SG92R, елевація SG90) наводять на нього дзеркало. Камера OV7670 додатково класифікує
стан неба (пряме сонце / хмарність / темрява) і слугує «свідком» — зберігає знімок, коли
освітленість різко падає. Журнал подій і кадри — у SPI-флеш W25Q64, керування й калібрування —
через UART CLI. Деталі: `docs/PRD.md`.

ЛР1 перевіряє лише середовище й апаратну базу (bring-up), без алгоритмів наведення й
класифікації — див. розділ «Статус після ЛР1» у `docs/PRD.md`.

## Швидкий старт (macOS / Linux)

```bash
git clone <URL цього репозиторію>
cd heliostat-sky-cam


make image      # один раз: зібрати Docker-образ з тулчейном
make build      # компіляція в Docker -> firmware/build/firmware.{elf,hex,bin}
make flash      # прошивка через ST-Link (потрібен stlink-tools: brew install stlink)
make monitor    # UART-термінал 115200 8N1 (picocom; на macOS: brew install picocom)
make test       # хостові тести (Docker, gcc), без плати
make coverage   # тести + поріг покриття 90 % (gcov)
make check-deps # перевірка правила залежностей між шарами
```

Для F446RE: `make build BOARD=F446`.

Читання логу без picocom (pyserial, крос-платформно):
```bash
pip3 install pyserial
python3 scripts/readlog.py --list        # знайти порт (STMicroelectronics STLink Virtual COM Port)
python3 scripts/readlog.py /dev/cu.usbmodemXXXX -s 8
```

**Windows (без make):** `scripts\build.bat` → `scripts\flash.bat` → PuTTY або
`python scripts\readlog.py COMx -s 8` на відповідному COM-порту, 115200 8N1.

Вимоги на хості: Docker Desktop, `stlink-tools` (`st-flash`, `st-info`), опційно `picocom`.

## Що має відбутися після прошивки

LED PA5 блимає, у терміналі з'являється банер, `SYSCLK = 100000000 Hz`, результати ініціалізації
USART2/I2C1/SPI2/ADC1(×4)/TIM3-PWM, блок `SELFTEST` (MPU-6050, OV7670, W25Q64, 4 канали
квадранта фоторезисторів) і щосекунди `tick ... TL=.. TR=.. BL=.. BR=..`. Кнопка B1 друкує
`[btn ] B1 pressed`.

## Структура репозиторію

```
docker/Dockerfile            образ тулчейну (ubuntu:22.04 + arm-none-eabi-gcc, gcc для тестів)
Makefile                     обгортка: image / build / flash / monitor / test / coverage / check-deps / clean
scripts/readlog.py           читання UART-логу без термінала (pyserial)
scripts/check_deps.py        перевірка правила залежностей між шарами (в CI)
firmware/                    проєкт STM32 (HAL), шарова структура
  Core/                      main, тактування, MSP, syscalls (printf -> UART)
  Common/                    кільцевий буфер без залежностей (ringbuf)
  Platform/                  board.h (піни), реалізація шин поверх HAL / bit-bang SCCB, ADC, серво
  Devices/                   драйвери MPU-6050, W25Q64, OV7670 (залежать лише від bus.h)
  App/                       логіка продукту: трекер, журнал, кадри, калібровка, клас неба, статистика (perf), облік задач (rtstats), CLI, selftest (без HAL)
  Core/Src/product_rtos.c    ЛР4: 12 задач FreeRTOS, черги, м'ютекси; rtos_port.c — хуки, printf, тік; rtos_tests.c — сценарії п. 7.2
  Middlewares/FreeRTOS/      ядро FreeRTOS V11.1.0 (версія — docs/versions.md); прошивка ЛР3 (super loop) — git-тег lab3
  Drivers/                   HAL та CMSIS від ST (вендорені, версії: docs/versions.md)
tests/                       хостові тести: fake/mock шин, вектори, покриття
docs/PRD.md                  вимоги до продукту
docs/hardware-components.md  дослідження компонентів
docs/pinout.md               pinout, адреси, живлення, параметри шин, схема (ЛР2)
docs/architecture.md         шари, правила залежностей, component diagram (ЛР2)
docs/captures/               записи логічного аналізатора (ЛР2); розбір — у docs/lab2-report.md, розд. 3.5
docs/lab2-report.md          звіт ЛР2;  docs/problems.md — журнал проблем
docs/lab5-report.md          звіт ЛР5;  docs/qp-design.md — AO, події, власники ресурсів; docs/statecharts/ — діаграми; docs/comparison.md — три підходи
docs/lab4-report.md          звіт ЛР4;  docs/rtos-design.md — задачі, пріоритети, механізми; docs/comparison-lr3-lr4.md — порівняння з ЛР3
docs/lab3-report.md          звіт ЛР3;  docs/tasks.md — задачі; docs/limitations.md — реєстр обмежень;
                             docs/baseline-lab4.md — базова лінія для ЛР4; docs/metrics/ — логи прогонів
tools/                       хост-скрипти ЛР3: run_scenario.py, analyze_stats.py, analyze_latency.py, make_figures.py
docs/versions.md             версії HAL/CMSIS/тулчейну
.github/workflows/ci.yml     CI: check-deps, тести+покриття, збірка F411 та F446
```

## CLI (UART, 115200): символ у монітор запускає транзакцію для запису аналізатором

`h` help · `s` selftest · `i` I²C scan · `m` MPU · `c` камера PID/VER · `f` JEDEC flash · `w` flash erase/write/read · `p` PWM 1000/2000 мкс · `a` ADC + маркер D13 (PA5) · `e` навмисний NACK (0x69) · `r` потік MPU 60 с · `1`/`2` серво азимут/елевація · `x` XCLK on/off · `k` статистика I²C

Команди ЛР4 (додано, старі не змінено): `TASKS` · `STACK` · `HEAP` · `OSCOST` · `TRACE ARM|DUMP [n]` · `BTNSIM ms` · `FAULT SDA 1|0` · `CAMIRQ 0|1` · `TEST INV [SEM]|STK|STARVE ms|QFLOOD n ms|HEAP|CTX n|CAMDIAG ms`. Матриця вимірювань для обох прошивок: `python3 tools/measure_all.py --fw lr3|lr4 --bin ...`; порівняння: `python3 tools/compare_lr3_lr4.py`.

Команди ЛР5: `QSTAT` (черги, пули, втрати подій) · `TEST HOG|LOGRATE|BLOCK|OWNER|CTRL|ONESTEP|SNAPTO|FLASHFAIL|STARVE|POOL|STATUS|AUTO [n]|OFF`. Матриця: `python3 tools/measure_all.py --fw lr5 --bin ...`; сценарії: `python3 tools/lr5_auto.py`; фінальна таблиця трьох підходів: `python3 tools/compare_all.py`; рисунки: `python3 tools/make_figures_lr5.py`.

**Ліцензія QP/C.** `firmware/Middlewares/qpc` — QP/C 8.1.6 від Quantum Leaps під GPLv3 (використовуємо в навчальній роботі); для закритого продукту потрібна комерційна ліцензія.

Команди ЛР3 (рядки великими літерами, закінчуються `\r`): `VER` · `STATS [RESET]` · `MEM` · `MODE` · `SKY [FORCE CLEAR|OVERCAST|DARK|AUTO]` · `SERVO ON|OFF` · `CAL ...` · `LOG [n]` · `FRAMES [n]` · `SNAP` · `PERIOD <задача> <мс>` · `PROBE`. Приклад: `py scripts/sendcmd.py "LOG 8" --wait 3`. Сценарії вимірювань — `docs/baseline-lab4.md`.

Скрипти: `scripts/sendcmd.py <символ>` — надіслати команду з комп'ютера (для запису аналізатором), `make decode` / `make check-captures` — розбір записів.

## Піни

Дивитись `docs/pinout.md` (повна таблиця) та `firmware/Platform/Inc/board.h`.

| Сигнал | Пін | Призначення |
|--------|-----|-------------|
| USART2 TX/RX | PA2/PA3 | ST-Link VCP, 115200 8N1 |
| LED LD2 / кнопка B1 | PA5 / PC13 | вбудовані |
| ADC1_IN0/1/4/8 (TL/TR/BL/BR) | PA0/PA1/PA4/PB0 | квадрант GM5528 |
| TIM3 CH1/CH2 (AZ/EL) | PA6/PA7 | PWM 50 Гц, серво SG92R/SG90 (5V!) |
| I2C1 SCL/SDA | PB8/PB9 | MPU-6050 (опційно) |
| SCCB SIOC/SIOD (bit-bang) | PB10/PB4 (D6/D5) | камера OV7670 (SCCB ≠ I²C, див. docs/architecture.md) |
| SPI2 SCK/MISO/MOSI, CS | PB13/PB14/PB15, PB12 | W25Q64 |
| MCO1 (XCLK) | PA8 | тактовий сигнал OV7670 |
