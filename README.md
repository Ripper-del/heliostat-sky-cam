# Геліостат з камерою класифікації неба — ЛР1: bring-up STM32

Навчальний проєкт з курсу «Системне програмування вбудованих систем».
Платформа: NUCLEO-F411RE / F446RE, STM32 HAL, C, збірка в Docker.

Команда: Андрій Плешу, Тимур Варшавський, група ІО-36.

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
cd incident-recorder-lab1


make image      # один раз: зібрати Docker-образ з тулчейном
make build      # компіляція в Docker -> firmware/build/firmware.{elf,hex,bin}
make flash      # прошивка через ST-Link (потрібен stlink-tools: brew install stlink)
make monitor    # UART-термінал 115200 8N1 (picocom; на macOS: brew install picocom)
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
docker/Dockerfile            образ тулчейну (ubuntu:22.04 + arm-none-eabi-gcc)
Makefile                     обгортка: image / build / flash / monitor / clean
scripts/readlog.py           читання UART-логу без термінала (pyserial)
scripts/build.bat            Windows: збірка в Docker
scripts/flash.bat            Windows: копіювання firmware.bin на диск NODE_F411RE
firmware/                    проєкт STM32 (HAL)
  Core/                      main, тактування, MSP (піни), syscalls (printf -> UART)
  App/                       проби датчиків (I²C scan, MPU-6050, OV7670, W25Q, квадрант ADC)
  Drivers/                   HAL та CMSIS від ST
docs/PRD.md                  вимоги до продукту (геліостат + камера)
docs/hardware-components.md  дослідження компонентів
docs/report.md                звіт ЛР1
docs/screenshots/            скріншоти CubeMX, UART, логічного аналізатора
.github/workflows/ci.yml     CI: збірка F411 та F446 на кожен PR
```

## Піни

Дивись `firmware/Core/Inc/main.h` та `docs/hardware-components.md`.

| Сигнал | Пін | Призначення |
|--------|-----|-------------|
| USART2 TX/RX | PA2/PA3 | ST-Link VCP, 115200 8N1 |
| LED LD2 / кнопка B1 | PA5 / PC13 | вбудовані |
| ADC1_IN0/1/4/8 (TL/TR/BL/BR) | PA0/PA1/PA4/PB0 | квадрант GM5528 |
| TIM3 CH1/CH2 (AZ/EL) | PA6/PA7 | PWM 50 Гц, серво SG92R/SG90 (5V!) |
| I2C1 SCL/SDA | PB8/PB9 | MPU-6050 (опційно) + SCCB камери OV7670 |
| SPI2 SCK/MISO/MOSI, CS | PB13/PB14/PB15, PB12 | W25Q64 |
| MCO1 (XCLK) | PA8 | тактовий сигнал OV7670 |
