# EchoDesk OS v1.0 build report

## Target

- PlatformIO environment: `freenove-fnk0114a-2p4`
- MCU: ESP32, 4 MB flash
- Display layout: 320x240 landscape
- Protocol: existing AI Monitor USB serial JSON/framed protocol, 115200 baud

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
- SVG preview parses as valid XML.

## Final build result

- Result: **SUCCESS**
- RAM: 119,836 / 327,680 bytes (36.6%)
- Application flash: 1,314,765 / 1,966,080 bytes (66.9%)
- Merged image size: 1,386,880 bytes
- Merged image offset: `0x0`
- SHA-256:
  `CE820EC683F9F3C87889FCDF3661317BBCFED320B3AF0786504F0F6FB09B4A8D`

## Hardware status

No firmware was written to the physical device as part of this EchoDesk OS
build. Hardware rendering and touch behavior remain pending a separately
authorised flash/test pass.
