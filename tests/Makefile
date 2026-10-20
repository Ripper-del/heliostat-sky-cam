# Хостові тести (gcc, без плати): make -C tests [test|coverage|clean]
# Покриття (gcov) рахується для шарів Devices/ та App/ (чиста логіка); поріг — COV_MIN %.
CC      = gcc
FW      = ../firmware
INC     = -I. -Ifakes -Ivectors -I$(FW)/Devices/Inc -I$(FW)/App/Inc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O0 -g --coverage $(INC)
COV_MIN ?= 90
BUILD   = build

UNDER_TEST = $(FW)/Devices/Src/mpu6050.c $(FW)/Devices/Src/w25q.c $(FW)/Devices/Src/ov7670.c $(FW)/App/Src/quadrant.c
TESTS      = test_main.c test_mpu6050.c test_w25q.c test_ov7670.c test_quadrant.c
FAKES      = fakes/fake_i2c.c fakes/fake_w25q.c fakes/mock_sccb.c

.PHONY: test coverage clean
test: $(BUILD)/run_tests
	@rm -f $(BUILD)/*.gcda
	cd $(BUILD) && ./run_tests

$(BUILD)/run_tests: $(UNDER_TEST) $(TESTS) $(FAKES) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD):
	mkdir -p $@

# Запускає тести та перевіряє поріг покриття рядків для кожного файлу під тестом.
coverage: test
	@fail=0; for f in $(basename $(notdir $(UNDER_TEST))); do \
	  pct=$$(gcov -n -o $(BUILD) $(BUILD)/run_tests-$$f.gcda 2>/dev/null | awk '/^File/{w=($$0 ~ /\/(Devices|App)\/Src\//)} w && /^Lines executed/{split($$2,a,"[:%]"); print a[2]; exit}'); \
	  printf "  %-10s %s%%\n" $$f "$$pct"; \
	  awk -v p="$$pct" -v m=$(COV_MIN) 'BEGIN{exit !(p+0 >= m)}' || fail=1; \
	done; \
	if [ $$fail = 1 ]; then echo "Coverage below $(COV_MIN)%"; exit 1; else echo "Coverage >= $(COV_MIN)% for all tested files"; fi

clean:
	rm -rf $(BUILD)
