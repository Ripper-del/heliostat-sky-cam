# Кореневий Makefile — обгортка над Docker-збіркою, прошивкою та UART-монітором.
#
#   make image     зібрати Docker-образ тулчейну (один раз)
#   make build     скомпілювати прошивку всередині Docker -> firmware/build/firmware.{elf,hex,bin}
#   make flash     прошити плату через ST-Link (stlink-tools: st-flash)
#   make monitor   відкрити UART-термінал (picocom, 115200 8N1)
#   make clean     видалити артефакти збірки
#
# Змінні:
#   BOARD=F411 (за замовч.) або F446      — яка Nucleo
#   PORT=/dev/ttyACM0                     — послідовний порт для monitor

IMAGE ?= stm32-build
BOARD ?= F411
PORT  ?= $(firstword $(wildcard /dev/ttyACM* /dev/tty.usbmodem*))
BIN    = firmware/build/firmware.bin

.PHONY: image build flash monitor clean shell

image:
	docker build -t $(IMAGE) docker/

# Якщо образу ще немає — збираємо його автоматично.
build:
	@docker image inspect $(IMAGE) >/dev/null 2>&1 || docker build -t $(IMAGE) docker/
	docker run --rm -v "$(CURDIR)":/workspace -w /workspace \
	  --user $$(id -u):$$(id -g) $(IMAGE) \
	  make -C firmware all BOARD=$(BOARD)

flash: build
	st-flash --reset write $(BIN) 0x08000000

monitor:
	@test -n "$(PORT)" || (echo "Порт не знайдено. Вкажіть: make monitor PORT=/dev/ttyACM0"; exit 1)
	picocom -b 115200 --imap lfcrlf $(PORT)

clean:
	rm -rf firmware/build

shell:
	docker run --rm -it -v "$(CURDIR)":/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE) bash
