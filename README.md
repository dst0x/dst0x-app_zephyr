# RSC minimal firmware

Minimal Zephyr LED-blink application for the WeAct Studio STM32F412RET6
CoreBoard. The application contains no UART console, logging, USB, SD card,
networking, or filesystem support.

The complete workspace is located at `RSC1.0`:

- `zephyr/`: Zephyr v4.5.0-rc1 source
- `modules/hal/`: only CMSIS and the STM32F4 HAL needed by this board
- `.venv/`: the local Python environment
- `rsc-app/`: this application and its custom board definition
- `build/rsc-app/`: generated build files

## Build

Run from the `RSC1.0` directory:

```sh
source .venv/bin/activate
export ZEPHYR_BASE="$PWD/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
export GNUARMEMB_TOOLCHAIN_PATH=/usr
export USE_CCACHE=0
west build -p always -b weact_stm32f412_core \
  -d build/rsc-app rsc-app
```

## Flash with ST-Link

```sh
west flash -d build/rsc-app -r openocd
```

Connect ST-Link `SWDIO`, `SWCLK`, `GND`, and `NRST` to the board before
flashing. Power the target from only one appropriate source.

If OpenOCD reports `unable to connect to the target`, check these signals:

- ST-Link `SWDIO` to MCU `PA13`
- ST-Link `SWCLK` to MCU `PA14`
- ST-Link `GND` to board `GND`
- ST-Link `NRST` to board `NRST`
- ST-Link voltage reference to board `3V3`
