#!/bin/bash
# Flash RSC1.0 firmware via ST-LINK GDB Server

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ELF="$PROJECT_DIR/build/zephyr/zephyr.elf"

GDB_SERVER="/opt/st/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.stlink-gdb-server.linux64_2.2.400.202601091506/tools/bin/ST-LINK_gdbserver"
CUBE_PATH="/opt/st/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.400.202601091506/tools/bin"
GDB="/opt/st/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_1.0.100.202602081740/tools/bin/arm-none-eabi-gdb"

if [ ! -f "$ELF" ]; then
    echo "ERROR: $ELF not found. Build first."
    exit 1
fi

"$GDB_SERVER" -p 61234 -l 1 -d -s -cp "$CUBE_PATH" -m 0 -k --halt 2>&1 &
GDB_PID=$!
sleep 3

"$GDB" -batch \
    -ex "target extended-remote :61234" \
    -ex "load" \
    -ex "set confirm off" \
    -ex "disconnect" \
    -ex "quit" \
    "$ELF" 2>&1

kill $GDB_PID 2>/dev/null
wait $GDB_PID 2>/dev/null

echo ""
echo "Flash complete. Press RESET on the board."
