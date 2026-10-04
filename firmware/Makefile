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
HAL_DIR = Drivers/STM32F4xx_HAL_Driver/Src
C_SOURCES = \
  $(wildcard Core/Src/*.c) \
  $(wildcard App/Src/*.c) \
  Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c \
  $(wildcard $(HAL_DIR)/*.c)
ASM_SOURCES = $(STARTUP)

# ---- Включення та визначення ----
C_INCLUDES = \
  -ICore/Inc -IApp/Inc \
  -IDrivers/STM32F4xx_HAL_Driver/Inc \
  -IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
  -IDrivers/CMSIS/Include
C_DEFS = -DUSE_HAL_DRIVER -D$(MCU_DEF)

# ---- Опції компіляції (Cortex-M4F) ----
MCU = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
OPT = -Og
CFLAGS  = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections -g -gdwarf-2 -MMD -MP
ASFLAGS = $(MCU) $(C_DEFS) $(OPT) -Wall -fdata-sections -ffunction-sections
LDSCRIPT = STM32F4xx_FLASH.ld
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) -lc -lm -lnosys \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

# ---- Правила ----
OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

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
