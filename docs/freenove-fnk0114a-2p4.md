# Freenove FNK0114A_2P4 firmware

This target runs the existing AI Monitor firmware and USB serial protocol on
the Freenove FNK0114A 2.4-inch display. It uses the unmodified dashboard,
including landscape layouts, touch window navigation, session and weekly
usage, and live reset countdowns.

## Verified hardware

The values below come from Freenove's official documentation, schematic,
board-specific TFT_eSPI setup, and touch examples:

| Function | Configuration |
| --- | --- |
| MCU | ESP32-WROOM-32E (`esp32dev`) |
| USB serial | CH340C to UART0: ESP32 TX GPIO1, RX GPIO3; 115200 baud |
| Display | ST7789, 240x320, BGR, inversion on, HSPI at 80 MHz |
| Display GPIO | MISO 12, MOSI 13, SCLK 14, CS 15, DC/RS 2 |
| Display reset | Tied to board reset (`TFT_RST=-1`) |
| Backlight | GPIO21, active high, PWM controlled |
| Touch | XPT2046-compatible resistive controller, separate software SPI |
| Touch GPIO | DOUT/MISO 39, DIN/MOSI 32, CS 33, CLK 25, PENIRQ 36 |
| Landscape | Display and touch rotation 1 (320x240) |
| Touch calibration | `527, 3552, 683, 3464, 320, 240, 1` |

Primary sources:

- [Freenove board model table](https://docs.freenove.com/projects/fnk0103/en/latest/fnk0103/codes/tutorial/Freenove_ESP32_Display.html)
- [Freenove display setup and landscape example](https://docs.freenove.com/projects/fnk0103/en/latest/fnk0103/codes/tutorial/10_TFT_Display.html)
- [Freenove touch pins and rotation example](https://docs.freenove.com/projects/fnk0103/en/latest/fnk0103/codes/tutorial/11_TFT_Touch_Calibration.html)
- [Freenove FNK0114A schematic](https://github.com/Freenove/Freenove_ESP32_Display/blob/6da6dae20f2d0b0bb85854691ac52dc5479f5c51/Datasheet/2.4inch_ST7789_V1.0/Schematic/2.4inch_ESP32-32E_Display_Schematic.pdf)
- [Freenove FNK0114A TFT_eSPI setup archive](https://github.com/Freenove/Freenove_ESP32_Display/blob/6da6dae20f2d0b0bb85854691ac52dc5479f5c51/Libraries/FNK0114A_2.4inch_ST7789/TFT_eSPI_Setups_v1.4.zip)
- [Freenove 2.4-inch touch drawing/calibration example](https://github.com/Freenove/Freenove_ESP32_Display/blob/6da6dae20f2d0b0bb85854691ac52dc5479f5c51/Sketches/Sketch_12.1_TFT_Touch_Draw_2.4_2.8_Inch/Sketch_12.1_TFT_Touch_Draw_2.4_2.8_Inch.ino)

## Build

Install PlatformIO, then run the reproducible Windows build-and-merge script
from the repository root:

```powershell
py -m pip install --user platformio
powershell -ExecutionPolicy Bypass -File .\scripts\build_freenove_firmware.ps1
```

The application image is written to:

```text
.pio/build/freenove-fnk0114a-2p4/firmware.bin
```

The complete, single-file image generated for flashing at address `0x0` is:

```text
dist/ai-monitor-freenove-fnk0114a-2p4.bin
```

## Flash on Windows

Do not use the AI Monitor companion's built-in ST7789 firmware updater for
this board. That release asset targets the CYD, not the Freenove board. The
companion remains fully compatible for normal usage data after the Freenove
firmware is installed manually.

1. Connect the board with a USB data cable. In Device Manager, note its
   `USB-SERIAL CH340 (COMx)` port. Install Freenove's CH340 driver if no COM
   port appears.
2. Close AI Monitor and any serial terminal so the COM port is free.
3. Install PlatformIO (`py -m pip install --user platformio`) if needed.
4. Optionally select `Landscape right` in AI Monitor after the first
   connection; this is Freenove's documented rotation-1 landscape direction.
5. Erase and flash the merged image, replacing `COM5` with the detected port:

```powershell
py "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32 --port COM5 --baud 460800 erase_flash
py "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32 --port COM5 --baud 460800 write_flash 0x0 .\dist\ai-monitor-freenove-fnk0114a-2p4.bin
```

The build script prints the generated binary's size and SHA-256 and refreshes
`dist/ai-monitor-freenove-fnk0114a-2p4.bin.sha256` on every successful build.

PowerShell uses the backtick rather than `\` for line continuation. The
commands above are easiest to paste as single lines, or replace each trailing
`\` with a PowerShell backtick.

No physical-device flashing is part of the build process. Only run the flash
commands after explicitly confirming the correct COM port and board.

After flashing, reopen AI Monitor for Windows. It will discover the CH340 COM
port, perform the existing `get_info` handshake, and send CodexBar usage data
over the unchanged 115200-baud protocol.
