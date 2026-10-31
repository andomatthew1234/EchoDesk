# EchoDesk OS v1.0 build report

## Target

- PlatformIO environment: `freenove-fnk0114a-2p4`
- MCU: ESP32, 4 MB flash
- Display layout: 320x240 landscape
- Protocol: existing AI Monitor USB serial JSON/framed protocol plus negotiated
  Performance v1, Music v1, background-image v1 and visualizer v1 frames,
  115200 baud

## Static validation

- EchoDesk source is gated by `ECHODESK_OS` in only the Freenove environment.
- Legacy CYD/S3 UI paths remain selected when that flag is absent.
- OpenAI data is read from the existing provider cache; no usage values are
  generated in firmware.
- Clock UTC input still comes from companion frames or NTP; local rendering
  uses the Australia/Melbourne DST rule.
- Brightness uses the existing GPIO21 LEDC implementation and existing NVS
  config store.
- Restart is deferred outside its LVGL callback.
- Performance history uses a fixed 60-sample ring; missing metrics and sensors
  retain explicit validity bits and render as `N/A`.
- Performance frames are capability-gated and share the existing serial owner;
  the LibreHardwareMonitor helper never opens COM.
- Temperature Pressure uses the highest available component pressure with
  asymmetric smoothing and hysteresis; it is labelled as an estimate.
- `scripts/test_performance_data_native.ps1`: PASS (history wrap, freshness,
  missing data, validation, pressure, hysteresis and network scaling).
- `scripts/test_music_data_native.ps1`: PASS (freshness, local progress,
  metadata validation, bounded base64, artwork identity, CRC and atomic swap).
- `scripts/test_now_playing_layout_native.ps1`: PASS (all three 320x240
  geometries, all nine layout/background combinations, per-layout defaults,
  explicit override retention, touch-target minimums, progress clamping,
  corner states and bar smoothing).
- Music-data tests cover the exact-size two-slot 320×240 JPEG cache, current-art
  identity, baseline encoded-dimension validation, retired-buffer lifetime,
  corrupt/out-of-order preservation, and 16-band freshness states.
- WASAPI capture is opt-in, follows the Windows default multimedia endpoint,
  performs a Hann-windowed 1,024-point FFT at about 20 Hz, and transmits only
  16 byte bands. Driver silence decays instead of freezing old samples.
- The Windows companion frontend TypeScript/Vite production build passed.
- `cargo metadata --no-deps` and standalone `rustfmt --check` of the GSMTC and
  WASAPI modules passed. A full Rust type-check/test link was not possible on
  this workstation because the MSVC linker and Windows SDK import libraries
  are not installed; the Windows CI workflow remains authoritative.
- Clean `esp32dev` (ILI9341) and `esp32dev-st7789` legacy builds both passed,
  confirming the new telemetry allocation and parsing stay EchoDesk-only.
- SVG preview parses as valid XML.

## Final build result

- Result: **SUCCESS**
- RAM: 124,476 / 327,680 bytes (38.0%)
- Application flash: 1,392,529 / 1,966,080 bytes (70.8%)
- Merged image size: 1,464,640 bytes
- Merged image offset: `0x0`
- SHA-256:
  `421FB8D633D21DA51A7BDB53C87089917639DE7DB606C0C3A32D5F4C413F1D7D`

## Hardware status

At the user's explicit request, the final Part 3 application image was written
to COM5 at offset `0x10000`, preserving NVS, after a final audit strengthened
the LVGL background-buffer handoff and encoded JPEG-dimension checks.
`verify_flash` matched all 1,399,104 application bytes. The rebooted device
reported firmware `2.27.0`, ESP32-D0WD-V3 revision 3.1, 40 MHz crystal, ST7789,
landscape-right, both Part 3 capabilities, 191,992 bytes free heap and zero
frame glitches. The merged release image above was reproduced from that same
final source.

Physical touch feel, all nine on-panel combinations, NVS persistence across a
power cycle, live blurred-art transfer, actual WASAPI bars, endpoint changes,
long-running heap behaviour and international glyph coverage still require
hands-on use. The updated Windows backend also needs a machine/CI runner with
the MSVC Build Tools and Windows SDK before it can be packaged and installed.
