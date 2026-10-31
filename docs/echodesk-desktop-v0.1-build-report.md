# EchoDesk Desktop v0.1 build report

Build date: 31 October 2026 (Australia/Sydney)

## Result

EchoDesk Desktop v0.1 is implemented as a native .NET 8 WPF application. The
Windows distributable contains the UI, a hidden EchoDesk Host, the pinned
CLI-only Codex usage provider, and the EchoDesk performance helper. EchoDesk
Host is the only process that opens the USB serial port; the WPF application
uses the local `EchoDesk.Companion.v1` named pipe.

The firmware control protocol is versioned as `echoDeskControlProtocol: 1` and
the firmware version is 2.28.0. This task did not flash the physical device.

## Built artifacts

| Artifact | Size | SHA-256 |
|---|---:|---|
| `desktop/dist/EchoDesk-Desktop-v0.1-win-x64.zip` | 115,146,991 bytes | `9E81CDAF2933999F3FCAB6C99801A14CEB4F84898C1EAABE875ACFA8D80F263D` |
| `dist/ai-monitor-freenove-fnk0114a-2p4.bin` | 1,471,136 bytes | `7A924F81DF43C6D4E8FFD411217FD0E3B91D60E31F0AEC7EDDAB19144123E71C` |

The firmware file is a merged ESP32 image for offset `0x0`. It was built but
not written to the device.

## Supported device controls

- Automatic EchoDesk identification, reconnect, disconnect handling, and
  firmware/device/app reporting
- Home, Codex Usage, Clock, Performance, Now Playing, and Settings navigation
- Restart with desktop confirmation
- Persisted light/dark theme
- Brightness from 10–100%, with non-persistent previews and one persisted value
- Now Playing layout and background, including restore-layout-default behavior
- Device-originated active-app and settings changes
- Revision checks that reject stale settings writes

The firmware explicitly reports these v0.1 settings as unsupported, so the
desktop explains and disables them rather than simulating success:

- Default startup app
- 12/24-hour clock format and timezone
- Codex percentage mode and preferred display
- Performance default view

## Validation performed

- WPF solution build: passed with 0 warnings and 0 errors
- Desktop protocol/mock test executable: passed
- Performance helper self-test: passed
- Full Rust EchoDesk Host release build: passed using a checksum-verified
  Windows SDK/CRT sysroot
- EchoDesk Host unit tests: 44 passed, 0 failed
- Live named-pipe `hello` exchange against the packaged host: passed
- Integrated package smoke launch: passed; Desktop, Host, and performance helper
  all started from the packaged layout
- Freenove firmware release build: passed
  - RAM: 124,476 / 327,680 bytes (38.0%)
  - Flash: 1,399,021 / 1,966,080 bytes (71.2%)
- The native firmware control, usage-state, music-data, Now Playing layout, and
  performance-data test suites are part of the final validation run.
- The actual WPF window was visually inspected in Windows in light and dark
  themes; a primary-button foreground contrast defect found during inspection
  was corrected before this package was built.

## Physical testing still required

- Flash firmware 2.28.0 to the Freenove FNK0114A_2P4.
- Confirm COM discovery and the identity handshake against the physical CH340.
- Exercise navigation, settings, restart, sleep/wake recovery, and touchscreen
  to desktop synchronization on hardware.
- Confirm long-running coexistence of AI usage, Performance, and Now Playing
  feeds over the one serial owner.

No GitHub release was published, no website was deployed, and no physical
firmware flash was performed for this task.
