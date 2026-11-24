##########################################################################
# firmware/Makefile — збірка прошивки STM32F4 (HAL) за допомогою arm-none-eabi-gcc
#
#   make all               # збірка для NUCLEO-F411RE (за замовчуванням)
#   make all BOARD=F446    # збірка для NUCLEO-F446RE
#   make clean
#
# Зазвичай викликається з кореневого Makefile всередині Docker (make build).
##########################################################################

TARGET = firmware
BUILD_DIR = build
BOARD ?= F411
GIT_HASH ?= dev

ifeq ($(BOARD),F446)
  MCU_DEF  = STM32F446xx
  STARTUP  = Core/Startup/startup_stm32f446xx.s
else
  MCU_DEF  = STM32F411xE
  STARTUP  = Core/Startup/startup_stm32f411xe.s
endif

PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size

# ---- Джерела ----
# Шари (залежності лише зверху вниз, перевіряє scripts/check_deps.py):
#   Core -> App -> Devices;  Core/App -> Platform;  Platform -> Devices(bus.h) + HAL
HAL_DIR = Drivers/STM32F4xx_HAL_Driver/Src
# Лише потрібні модулі HAL (відповідають #define HAL_*_MODULE_ENABLED у stm32f4xx_hal_conf.h)
HAL_SOURCES = \
  $(addprefix $(HAL_DIR)/stm32f4xx_hal,\
    .c _cortex.c _rcc.c _rcc_ex.c _gpio.c _dma.c _dma_ex.c _flash.c _flash_ex.c \
    _pwr.c _pwr_ex.c _i2c.c _spi.c _uart.c _adc.c _adc_ex.c)
OWN_SOURCES = \
  $(wildcard Core/Src/*.c) \
  $(wildcard Common/Src/*.c) \
  $(wildcard Platform/Src/*.c) \
  $(wildcard Devices/Src/*.c) \
  $(wildcard App/Src/*.c)
# ЛР4: ядро FreeRTOS V11.1.0 (docs/versions.md), порт GCC/ARM_CM4F, heap_4. timers.c і event_groups.c не збираються (вимкнено в FreeRTOSConfig.h).
RTOS_DIR = Middlewares/FreeRTOS/Source
RTOS_SOURCES = $(addprefix $(RTOS_DIR)/,tasks.c queue.c list.c stream_buffer.c) \
  $(RTOS_DIR)/portable/GCC/ARM_CM4F/port.c $(RTOS_DIR)/portable/MemMang/heap_4.c
# ЛР5: QP/C 8.1.6 (submodule) з портом до FreeRTOS. qf_actq.c не потрібен (черги AO — черги FreeRTOS), вбудовані ядра QV/QK/QXK не збираються.
QPC = Middlewares/qpc
QP_SOURCES = $(addprefix $(QPC)/src/qf/,qep_hsm.c qep_msm.c qf_act.c qf_defer.c qf_dyn.c qf_mem.c qf_ps.c qf_qact.c qf_qeq.c qf_qmact.c qf_time.c) \
  $(QPC)/ports/freertos/qf_port.c
VENDOR_SOURCES = \
  Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c \
  $(HAL_SOURCES) $(RTOS_SOURCES) $(QP_SOURCES)
C_SOURCES = $(OWN_SOURCES) $(VENDOR_SOURCES)
ASM_SOURCES = $(STARTUP)

# ---- Включення та визначення ----
# Вендорські каталоги підключаються через -isystem: попередження в їхніх заголовках не показуються.
C_INCLUDES = \
  -ICore/Inc -IApp/Inc -IDevices/Inc -IPlatform/Inc -ICommon/Inc
VENDOR_INCLUDES = \
  -isystem $(RTOS_DIR)/include -isystem $(RTOS_DIR)/portable/GCC/ARM_CM4F \
  -isystem $(QPC)/include -isystem $(QPC)/ports/freertos \
  -isystem Drivers/STM32F4xx_HAL_Driver/Inc \
  -isystem Drivers/CMSIS/Device/ST/STM32F4xx/Include \
  -isystem Drivers/CMSIS/Include
# Варіанти збірки для сценаріїв: make all RTOS_FLAGS="-DRTOS_TICK_HZ=200" (ЛР5: -DBSP_STACK_SCALE_PCT=..., -DBSP_POOL1_N=..., -DLR5_FLASH_MUTEX, ...)
RTOS_FLAGS ?=
C_DEFS = -DUSE_HAL_DRIVER -D$(MCU_DEF) -DGIT_HASH=\"$(GIT_HASH)\" $(RTOS_FLAGS)

# ---- Опції компіляції (Cortex-M4F) ----
MCU = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
OPT = -Og
COMMON  = $(MCU) $(C_DEFS) $(C_INCLUDES) $(VENDOR_INCLUDES) $(OPT) -fdata-sections -ffunction-sections -g -gdwarf-2 -MMD -MP -fstack-usage
# Власний код: -Wall -Wextra, попередження = помилка збірки.
CFLAGS  = $(COMMON) -Wall -Wextra -Werror
# Вендорський код (HAL/CMSIS ST): попередження свідомо придушені (-w), лише для нього.
VENDOR_CFLAGS = $(COMMON) -w
ASFLAGS = $(MCU) $(C_DEFS) $(OPT) -Wall -fdata-sections -ffunction-sections
LDSCRIPT = STM32F4xx_FLASH.ld
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) -lc -lm -lnosys \
          -Wl,--wrap=printf,--wrap=puts,--wrap=putchar \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections -Wl,--print-memory-usage

# ---- Правила ----
OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
VENDOR_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(VENDOR_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(VENDOR_OBJECTS): $(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(VENDOR_CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(CP) -O ihex $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(CP) -O binary -S $< $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)
.PHONY: all clean
