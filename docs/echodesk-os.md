# EchoDesk OS v1.0

EchoDesk OS is the Freenove FNK0114A_2P4 user interface built on the existing
AI Monitor firmware. It keeps the verified Freenove display, touch,
backlight, NVS and CH340/UART implementation, and it keeps the existing
AI Monitor / Win-CodexBar serial protocol unchanged.

The feature is enabled only by `ECHODESK_OS=1` in the
`freenove-fnk0114a-2p4` PlatformIO environment. The CYD and ESP32-S3 targets
continue to compile their original dashboard and settings paths.

## UI architecture

`src/ui_echodesk.cpp` owns one persistent Home screen and at most one live app
screen. App screens are deleted after returning Home, which keeps LVGL object
memory bounded on the ESP32.

- **Home** — three 300x56 touch cards for Codex Usage, Clock and Settings.
- **Codex Usage** — reads the cached OpenAI provider state without changing the
  companion-managed active view. It displays the live 5-hour and weekly
  values, `PERCENT USED` or `PERCENT LEFT`, progress bars, reset countdowns and
  provider-specific connection freshness.
- **Clock** — uses the existing embedded DIN-style 76 px font. Eight offset
  white labels plus a centered black label form real outlined hour digits; the
  minute row is solid white. The ESP32 system clock is set by companion UTC
  frames and continues running between updates. Optional NTP can resynchronise
  it.
- **Settings** — contains only the 10–100% live backlight slider and Restart.
  Slider movement changes GPIO21 PWM immediately; release commits the value to
  NVS. Restart uses a confirmation overlay and defers `ESP.restart()` until
  after the LVGL event callback.

All app screens have a 54x26 Home control. Navigation uses 140 ms fades and a
350 ms input guard to reject accidental double taps. Serial parsing and NTP
ticks remain in the main loop regardless of the visible app.

## Timezone

EchoDesk uses the POSIX timezone rule:

```text
AEST-10AEDT,M10.1.0,M4.1.0/3
```

This represents Australia/Melbourne under the current daylight-saving rules.
The companion still supplies UTC time and the existing `tzOffsetMinutes`
field remains accepted; EchoDesk deliberately retains the full Melbourne DST
rule instead of reducing it to a fixed offset.

## Build

From the repository root on Windows:

```powershell
py -m pip install --user platformio
powershell -ExecutionPolicy Bypass -File .\scripts\build_freenove_firmware.ps1
```

Outputs:

```text
.pio/build/freenove-fnk0114a-2p4/firmware.bin
dist/ai-monitor-freenove-fnk0114a-2p4.bin
dist/ai-monitor-freenove-fnk0114a-2p4.bin.sha256
```

The file in `dist` is a merged ESP32 image containing the bootloader,
partition table, boot-app selector and application. It must be written at
offset `0x0`. The raw `firmware.bin` is the application-only image for offset
`0x10000`.

## Flash on Windows (manual, not performed by this build)

Do not use AI Monitor's built-in updater; its stock assets target other
hardware. Close AI Monitor and all serial terminals before flashing so the
CH340 port is free. Verify the checksum first:

```powershell
Get-FileHash .\dist\ai-monitor-freenove-fnk0114a-2p4.bin -Algorithm SHA256
Get-Content .\dist\ai-monitor-freenove-fnk0114a-2p4.bin.sha256
```

Identify the ESP32 before writing (replace `COM5` if Windows assigned another
port):

```powershell
py "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32 --port COM5 chip_id
```

Flash and verify the merged image at `0x0`:

```powershell
py "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32 --port COM5 --baud 460800 write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 .\dist\ai-monitor-freenove-fnk0114a-2p4.bin
py "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32 --port COM5 verify_flash 0x0 .\dist\ai-monitor-freenove-fnk0114a-2p4.bin
```

The merged file spans the configuration partition gap, so use the
application-only image at `0x10000` instead when deliberately preserving the
existing NVS configuration:

```powershell
py "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32 --port COM5 --baud 460800 write_flash 0x10000 .\.pio\build\freenove-fnk0114a-2p4\firmware.bin
```

After a manual flash, restart AI Monitor. It can continue using COM5 at 115200
baud and the same framed or legacy JSON protocol. No separate Windows app is
required.

## Validation boundary

The firmware build and merged image can be validated without touching the
device. Physical touch coordinates, visible orientation, backlight response,
panel typography and the Restart control still require a deliberate hardware
flash and hands-on test. They are not claimed as physically verified by this
build.

## Preview

The four-screen rendered preview is in
[`echodesk-os-preview.svg`](echodesk-os-preview.svg). Preview usage and time
values are labelled as sample data; the firmware never hardcodes those values.
