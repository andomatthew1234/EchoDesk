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

The final figures and SHA-256 are populated from the last clean build performed
after all implementation and audit fixes. See the adjacent checksum file for
the machine-readable digest.

## Hardware status

No firmware was written to the physical device as part of this EchoDesk OS
build. Hardware rendering and touch behavior remain pending a separately
authorised flash/test pass.
