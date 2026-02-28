# Corvus BMS Firmware — Dual-target Makefile
# make desktop  — gcc, mock HAL, desktop tests
# make test     — build + run tests
# make stm32    — arm-none-eabi-gcc (compile only, no link without full SDK)

CC_DESKTOP = gcc
CC_STM32   = arm-none-eabi-gcc
CFLAGS     = -Wall -Wextra -Werror -pedantic -std=c99 -Iinc
CFLAGS_DBG = $(CFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer

# Source files
SRC_CORE = src/bms_bq76952.c src/bms_monitor.c src/bms_protection.c \
           src/bms_contactor.c src/bms_can.c src/bms_state.c \
           src/bms_current_limit.c src/bms_soc.c src/bms_balance.c src/bms_nvm.c
SRC_RTOS = rtos/bms_tasks.c
SRC_TEST = test/test_main.c test/test_bq76952.c test/test_protection.c \
           test/test_contactor.c test/test_can.c test/test_state.c \
           test/test_current_limit.c test/test_soc.c test/test_balance.c test/test_nvm.c
HAL_MOCK = hal/hal_mock.c
HAL_STM32 = hal/hal_stm32f4.c

# Desktop build (mock HAL, no RTOS)
.PHONY: desktop test clean debug stm32

desktop: test_firmware

test_firmware: $(SRC_CORE) $(HAL_MOCK) $(SRC_TEST)
	$(CC_DESKTOP) $(CFLAGS) -DDESKTOP_BUILD -o $@ $^ -lm

debug: $(SRC_CORE) $(HAL_MOCK) $(SRC_TEST)
	$(CC_DESKTOP) $(CFLAGS_DBG) -DDESKTOP_BUILD -o test_firmware $^ -lm

test: test_firmware
	./test_firmware

# STM32 build — compile check only (no linker script / startup)
stm32:
	@echo "STM32 compile check (no link)..."
	$(CC_STM32) $(CFLAGS) -DSTM32_BUILD -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
		-c $(SRC_CORE) $(HAL_STM32) $(SRC_RTOS) main.c 2>&1 || echo "Note: arm-none-eabi-gcc not installed (expected on desktop)"
	@rm -f *.o

# Demo build — compile + run + plot
SRC_DEMO = demo/firmware_demo.c

firmware_demo: $(SRC_CORE) $(HAL_MOCK) $(SRC_DEMO)
	$(CC_DESKTOP) $(CFLAGS) -DDESKTOP_BUILD -o $@ $^ -lm

demo: firmware_demo
	./firmware_demo | python3 demo/plot_firmware.py

clean:
	rm -f test_firmware firmware_demo *.o
