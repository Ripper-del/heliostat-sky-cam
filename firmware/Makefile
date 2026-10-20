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
  $(wildcard Platform/Src/*.c) \
  $(wildcard Devices/Src/*.c) \
  $(wildcard App/Src/*.c)
VENDOR_SOURCES = \
  Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c \
  $(HAL_SOURCES)
C_SOURCES = $(OWN_SOURCES) $(VENDOR_SOURCES)
ASM_SOURCES = $(STARTUP)

# ---- Включення та визначення ----
# Вендорські каталоги підключаються через -isystem: попередження в їхніх заголовках не показуються.
C_INCLUDES = \
  -ICore/Inc -IApp/Inc -IDevices/Inc -IPlatform/Inc
VENDOR_INCLUDES = \
  -isystem Drivers/STM32F4xx_HAL_Driver/Inc \
  -isystem Drivers/CMSIS/Device/ST/STM32F4xx/Include \
  -isystem Drivers/CMSIS/Include
C_DEFS = -DUSE_HAL_DRIVER -D$(MCU_DEF)

# ---- Опції компіляції (Cortex-M4F) ----
MCU = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
OPT = -Og
COMMON  = $(MCU) $(C_DEFS) $(C_INCLUDES) $(VENDOR_INCLUDES) $(OPT) -fdata-sections -ffunction-sections -g -gdwarf-2 -MMD -MP
# Власний код: -Wall -Wextra, попередження = помилка збірки.
CFLAGS  = $(COMMON) -Wall -Wextra -Werror
# Вендорський код (HAL/CMSIS ST): попередження свідомо придушені (-w), лише для нього.
VENDOR_CFLAGS = $(COMMON) -w
ASFLAGS = $(MCU) $(C_DEFS) $(OPT) -Wall -fdata-sections -ffunction-sections
LDSCRIPT = STM32F4xx_FLASH.ld
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) -lc -lm -lnosys \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

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
