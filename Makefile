# Кореневий Makefile — обгортка над Docker-збіркою, прошивкою та UART-монітором.
#
#   make image     зібрати Docker-образ тулчейну (один раз)
#   make build     скомпілювати прошивку всередині Docker -> firmware/build/firmware.{elf,hex,bin}
#   make flash     прошити плату через ST-Link (stlink-tools: st-flash)
#   make monitor   відкрити UART-термінал (picocom, 115200 8N1)
#   make test      хостові тести (gcc у Docker), без плати
#   make coverage  тести + перевірка порогу покриття (gcov)
#   make check-deps перевірка правила залежностей між шарами
#   make decode    декодувати docs/captures/*.sr у .txt (sigrok-cli в Docker)
#   make check-captures  decode + перевірка, що на записах видно очікувані транзакції
#   make clean     видалити артефакти збірки
#
# Змінні:
#   BOARD=F411 (за замовч.) або F446      — яка Nucleo
#   PORT=/dev/ttyACM0                     — послідовний порт для monitor

IMAGE ?= stm32-build
BOARD ?= F411
PORT  ?= $(firstword $(wildcard /dev/ttyACM* /dev/tty.usbmodem*))
BIN    = firmware/build/firmware.bin

.PHONY: image build flash monitor clean shell test coverage check-deps decode check-captures check-pipeline

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

DOCKER_RUN = docker run --rm -v "$(CURDIR)":/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE)

test:
	@docker image inspect $(IMAGE) >/dev/null 2>&1 || docker build -t $(IMAGE) docker/
	$(DOCKER_RUN) make -C tests test

coverage:
	@docker image inspect $(IMAGE) >/dev/null 2>&1 || docker build -t $(IMAGE) docker/
	$(DOCKER_RUN) make -C tests coverage

check-deps:
	python3 scripts/check_deps.py

# Текстове декодування записів docs/captures/*.sr (sigrok-cli у Docker) і перевірка очікуваного вмісту
decode:
	@docker image inspect $(IMAGE) >/dev/null 2>&1 || docker build -t $(IMAGE) docker/
	$(DOCKER_RUN) scripts/decode_captures.sh

# Самоперевірка конвеєра розбору на синтетичних сигналах (без заліза): генератор -> sigrok-cli -> перевірка
check-pipeline:
	@docker image inspect $(IMAGE) >/dev/null 2>&1 || docker build -t $(IMAGE) docker/
	python3 scripts/synth_vcd.py
	$(DOCKER_RUN) env CAPDIR=/workspace/scripts/fixtures EXT=vcd scripts/decode_captures.sh
	python3 scripts/check_captures.py --dir scripts/fixtures

check-captures: decode
	python3 scripts/check_captures.py

clean:
	rm -rf firmware/build tests/build

shell:
	docker run --rm -it -v "$(CURDIR)":/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE) bash
