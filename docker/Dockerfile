# Версії arm-none-eabi-* зафіксовані (apt pin) — див. docs/versions.md.
# Тулчейн-образ для збірки прошивки STM32 (Cortex-M4).
# Збірка:  docker build -t stm32-build docker/
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# make                     — запуск Makefile
# gcc, libc6-dev           — хостовий gcc/gcov для `make test` (тести без плати)
# gcc-arm-none-eabi        — крос-компілятор під Cortex-M
# binutils-arm-none-eabi   — objcopy/size/objdump
# libnewlib-arm-none-eabi  — libc (printf/stdio) для bare-metal
# srecord                  — конвертація ELF/HEX/BIN
# sigrok-cli               — текстове декодування записів логічного аналізатора (scripts/decode_captures.sh)
RUN apt-get update && apt-get install -y --no-install-recommends \
    make gcc libc6-dev \
    gcc-arm-none-eabi=15:10.3-2021.07-4 \
    binutils-arm-none-eabi=2.38-3ubuntu1+15build1 \
    libnewlib-arm-none-eabi=3.3.0-1.3 \
    libstdc++-arm-none-eabi-newlib=15:10.3-2021.07-4+17 \
    python3 git ca-certificates srecord sigrok-cli \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
