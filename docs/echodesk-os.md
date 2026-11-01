# EchoDesk OS v1.0

EchoDesk OS is the Freenove FNK0114A_2P4 user interface built on the existing
AI Monitor firmware. It keeps the verified Freenove display, touch,
backlight, NVS and CH340/UART implementation, and it keeps the existing
AI Monitor / Win-CodexBar compatibility while extending the serial protocol
with an explicitly negotiated Performance v1 frame.

The feature is enabled only by `ECHODESK_OS=1` in the
`freenove-fnk0114a-2p4` PlatformIO environment. The CYD and ESP32-S3 targets
continue to compile their original dashboard and settings paths.

## UI architecture

`src/ui_echodesk.cpp` owns one persistent Home screen and at most one live app
screen. App screens are deleted after returning Home, which keeps LVGL object
memory bounded on the ESP32.

- **Home** — five large touch cards in a non-scrolling 2×3 layout for Codex
  Usage, Clock, Performance, Now Playing and Settings.
- **Codex Usage** — reads the cached OpenAI provider state without changing the
  companion-managed active view. It displays the live 5-hour and weekly
  values, `PERCENT USED` or `PERCENT LEFT`, progress bars, reset countdowns and
  provider-specific connection freshness. When the provider has no active
  5-hour window, that card is omitted rather than showing an invented zero.
  Unavailable usage data is represented by an animated loading spinner.
- **Clock** — uses the existing embedded DIN-style 76 px font. Eight offset
  white labels plus a centered black label form real outlined hour digits; the
  minute row is solid white. The ESP32 system clock is set by companion UTC
  frames and continues running between updates. Optional NTP can resynchronise
  it. Unsynchronised time uses an animated loading spinner.
- **Performance** — opens the configured default view (Overview, CPU, GPU or
  Memory). Usage and
  temperature selectors each provide five large targets. The aggregate
  dashboards show four 60-sample mini-graphs; individual views show full-width
  graphs. Missing sensors are `N/A`, data becomes stale after 3 seconds and is
  removed after 15 seconds offline. Temperature Pressure is a smoothed,
  hysteretic estimate based on the highest component-specific pressure, with
  GOOD/MODERATE/HIGH colour status and the contributing component.
- **Now Playing** — shows the selected Windows media session's title, artist,
  album, application, confirmed state, locally smoothed position, duration and
  96×96 artwork. Previous, play/pause and next are enabled only when GSMTC
  reports support. Missing metadata and artwork use explicit fallbacks; stale
  or offline data disables controls. Its local customisation panel independently
  persists Album Cover, CarPlay Inspired or EchoDesk Style and Album Cover,
  EchoDesk or Audio Bars backgrounds. All nine combinations are valid. Layout
  changes apply the layout's default background only until the user explicitly
  overrides it; Restore Layout Default clears that override. Windows prepares
  a cropped, blurred 320×240 background JPEG and sends it only while requested.
  EchoDesk is pure black/white according to the global theme. Audio Bars opts
  into local WASAPI loopback, renders 16 real FFT bands at 25 FPS with peak
  decay, and settles on pause/stop or stale data. No PCM reaches the ESP32.
  The four EchoDesk artwork brackets still animate by two pixels at 25 FPS only
  during confirmed playback. The companion remains the sole COM-port owner.
- **Settings** — contains the 10–100% live backlight slider, a persisted
  Dark/Light theme control, and Restart. The Light theme inverts the product
  palette to white surfaces with black type while retaining grey hierarchy.
  Slider movement changes GPIO21 PWM immediately; release commits the value to
  NVS. Restart uses a confirmation overlay and defers `ESP.restart()` until
  after the LVGL event callback. Factory Reset has its own confirmation overlay;
  it clears onboarding completion and language, retains firmware and unrelated
  preferences, then returns to the Hello screen.

## Guided first boot

Firmware 2.31.0 includes the persisted first-boot flow and adds optional,
protected Wi-Fi provisioning. USB remains selected by default. The Wi-Fi path
uses an expiring `EchoDesk-XXXX` access point and the version-1 protocol in
[`wifi-setup-protocol.md`](wifi-setup-protocol.md).

Firmware 2.32.0 adds Clock timers, Notification Stack, PC Dashboard, My Device
health, and staged on-device update progress while retaining the same setup
and recovery paths.

Firmware 2.30.0 introduced the version-1 setup
handshake documented in [`guided-device-setup.md`](guided-device-setup.md).
Blank devices show the animated Hello screen, English selection and the
configurable Desktop download prompt. Existing configured devices migrate to
`completed` so a firmware update cannot unexpectedly force onboarding.

All app screens have a Home control; Now Playing uses a 68x36 target for the
resistive touchscreen. Navigation uses 140 ms fades and a
350 ms input guard to reject accidental double taps. Serial parsing and NTP
ticks remain in the main loop regardless of the visible app.

## Desktop-controlled preferences

Firmware 2.29.0 and later persist and advertise all EchoDesk Desktop settings through
Desktop Control v1: startup app, 12/24-hour clock, named timezone, Codex
used/remaining presentation, preferred Codex card layout and Performance
default view. Partial settings patches remain revision-checked and atomic.

Timezone values are the IANA names `Australia/Melbourne`, `Australia/Sydney`
and `UTC`. The two Australian zones currently use the equivalent POSIX rule:

```text
AEST-10AEDT,M10.1.0,M4.1.0/3
```

The companion still supplies UTC time and the existing `tzOffsetMinutes`
field remains accepted for backwards compatibility; EchoDesk deliberately
retains the configured named timezone and its daylight-saving rule instead of
reducing it to a transient fixed offset. UTC uses `UTC0`.

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

After a manual flash, restart the updated AI Monitor Windows companion. It can
continue using COM5 at 115200 baud and remains the only serial-port owner. Its
bundled LibreHardwareMonitor helper supplies Performance telemetry through the
same serial thread; no second COM-port application is required.

## Validation boundary

The firmware build and merged image can be validated without touching the
device. Physical touch coordinates, visible orientation, backlight response,
panel typography and the Restart control still require a deliberate hardware
flash and hands-on test. They are not claimed as physically verified by this
build.

## Preview

The shell preview remains in [`echodesk-os-preview.svg`](echodesk-os-preview.svg).
The exact-aspect Performance selector, usage dashboard and pressure view are in
[`performance-centre-preview.svg`](performance-centre-preview.svg). Preview
values are labelled as sample data; production firmware never hardcodes them.
Feishin's supported GSMTC configuration and the optional credential-safe
Remote fallback design are documented in
[`feishin-windows-media-research.md`](feishin-windows-media-research.md).
