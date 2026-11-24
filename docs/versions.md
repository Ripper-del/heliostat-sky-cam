# Версії залежностей (фіксація для відтворюваної збірки)

| Компонент | Версія | Де зафіксовано / звідки |
|-----------|--------|--------------------------|
| STM32F4xx HAL Driver (vendored) | V1.8.5 | `firmware/Drivers/STM32F4xx_HAL_Driver/` — копія в репозиторії; версія в `Src/stm32f4xx_hal.c` (`__STM32F4xx_HAL_VERSION_*`) |
| CMSIS Device STM32F4xx (vendored) | V2.6.11 | `firmware/Drivers/CMSIS/Device/ST/STM32F4xx/` — `Include/stm32f4xx.h` (`__STM32F4xx_CMSIS_VERSION_*`) |
| CMSIS Core (vendored) | з того ж набору Drivers/CMSIS/Include | у репозиторії, не завантажується під час збірки |
| arm-none-eabi-gcc | 15:10.3-2021.07-4 (GCC 10.3.1) | `docker/Dockerfile`, apt pin |
| binutils-arm-none-eabi | 2.38-3ubuntu1+15build1 | `docker/Dockerfile`, apt pin |
| libnewlib-arm-none-eabi | 3.3.0-1.3 | `docker/Dockerfile`, apt pin |
| Базовий образ | `ubuntu:22.04` (тег; дайджест не зафіксовано — див. нижче) | `docker/Dockerfile` |
| Хостовий gcc/gcov для тестів | GCC 11.4 (Ubuntu 22.04) | `docker/Dockerfile` (`gcc`) |
| Тестовий каркас | власний (`tests/test.h`), сторонніх залежностей немає | — |
| FreeRTOS-Kernel (ЛР4) | **V11.1.0**, коміт `dbf70559b27d39c1fdb68dfb9a32140b6a6777a0`, порт GCC/ARM_CM4F, heap_4 | `firmware/Middlewares/FreeRTOS/` (копія файлів ядра без змін, джерело і перелік файлів — `VERSION.txt` там); `https://github.com/FreeRTOS/FreeRTOS-Kernel`, тег `V11.1.0` |

Єдина стороння бібліотека (з ЛР4) — ядро FreeRTOS; її вендорено в репозиторії (`firmware/Middlewares/FreeRTOS/`), HAL/CMSIS теж вендорені,
тому нічого не підтягується з мережі під час збірки, окрім apt-пакетів тулчейну в Docker-образі. Прошивка ЛР3 без ОС лишається збираною з тега `lab3`.

Обмеження: тег `ubuntu:22.04` рухомий (отримує security-оновлення), тому повної бітової відтворюваності
через рік це не гарантує; для цього треба зафіксувати дайджест образу (`ubuntu@sha256:...`) після
першої успішної CI-збірки. HAL, CMSIS і версії компілятора зафіксовані жорстко.
