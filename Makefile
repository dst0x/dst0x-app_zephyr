BOARD         := weact_stm32f412_core
ELF           := build/zephyr/zephyr.elf

export ZEPHYR_BASE           := $(HOME)/Documents/Dani/Codes/RSC1.0/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT := gnuarmemb
export GNUARMEMB_TOOLCHAIN_PATH := /usr

GDB_SERVER    := /opt/st/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.stlink-gdb-server.linux64_2.2.400.202601091506/tools/bin/ST-LINK_gdbserver
CUBE_PATH     := /opt/st/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin
GDB           := /opt/st/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin/arm-none-eabi-gdb
GDB_PORT      := 61234

VENV          := $(HOME)/zephyrproject/.venv/bin

.PHONY: build clean flash

build:
	$(VENV)/west build -b $(BOARD) -p auto

clean:
	rm -rf build

flash: build
	@"$(GDB_SERVER)" -p $(GDB_PORT) -l 1 -d -s -cp "$(CUBE_PATH)" -m 0 -k --halt 2>&1 & \
	GDB_PID=$$!; \
	sleep 3; \
	"$(GDB)" -batch \
		-ex "target extended-remote :$(GDB_PORT)" \
		-ex "load" \
		-ex "set confirm off" \
		-ex "disconnect" \
		-ex "quit" \
		"$(ELF)" 2>&1; \
	kill $$GDB_PID 2>/dev/null; \
	wait $$GDB_PID 2>/dev/null; \
	echo ""; \
	echo "Flash complete. Press RESET on the board."
