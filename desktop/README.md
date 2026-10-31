# EchoDesk Desktop 0.2

EchoDesk Desktop is the native Windows 11 control, customisation and component
app for EchoDesk. It is WPF/.NET 8 and contains no browser or Electron runtime.

## One app, one serial owner

The distributable includes a headless `EchoDesk Host.exe`. EchoDesk Desktop
starts it before connecting, restarts it if it exits unexpectedly, and closes it
when Desktop exits. The host is the **only** process that opens the USB COM port;
the WPF UI communicates over the current-user named pipe
`EchoDesk.Companion.v1` and never attempts a direct-serial fallback. COM5 is not
hardcoded.

No separate AI Monitor or visible Win-CodexBar app is required. The package also
contains the CLI-only Codex usage provider and the EchoDesk performance helper.
Codex usage still requires the normal Codex CLI to be signed in for this Windows
account and an internet connection. EchoDesk never asks for, displays, or stores
your provider credentials.

## Install and run

1. Extract `EchoDesk-Desktop-v0.2.2-win-x64.zip` completely.
2. Run `install.ps1` from PowerShell for a current-user Start menu installation,
   or run `EchoDesk Desktop.exe` directly from the extracted folder.
3. Connect the EchoDesk display by USB. The bundled host starts invisibly and
   reconnects automatically.

The self-contained build does not require a separately installed .NET runtime.
Closing the window minimises to the tray by default. Use **Exit** in the tray to
stop all bundled EchoDesk background processes and release the COM port.

## Setup & Updates

The **Setup & Updates** destination provides first-time EchoDesk OS installation,
updates, reinstall and guided recovery. It only accepts device candidates and
firmware metadata supplied by the local EchoDesk Host; the WPF interface does
not accept paths, flash offsets or arbitrary flash commands from users.

An EchoDesk application handshake identifies an existing installation. A CH340
serial adapter without that handshake is treated only as a candidate: the user
must select EchoDesk Mini and explicitly confirm the physical Freenove
FNK0114A_2P4 board before the host may continue with its ESP32 compatibility
checks.

The self-contained package includes the verified EchoDesk Mini firmware and an
in-process ESP32 flashing engine. Python, PlatformIO and a separate esptool
download are not required. Firmware installation always requires an explicit
click and is never started silently. No public release URL is used until an
official EchoDesk GitHub Release exists.

The installer displays a numeric percentage only when the flashing engine
reports measured progress. It declares success only after the write completes
and the new firmware restarts and reconnects. A completed write without a
verified reconnect is reported as a partial installation with recovery steps.

## AI Usage Monitor

The Overview page contains an AI Usage Monitor component card. Its Enabled
switch is authoritative: the host persists the setting, stops background usage
polling when disabled, and returns the confirmed state. The card reports
background activity, last successful update and safe guidance for sign-in,
expired sessions, network failures and rate limits. Sign in through the official
Codex CLI if requested; do not paste credentials into EchoDesk.

## Supported controls

- Automatic host, broker and device reconnection
- Reported connection, COM port, firmware, hardware and active app
- AI Usage Monitor enable/disable, status and last successful update
- Open advertised touchscreen apps and restart the device with confirmation
- Device light/dark theme, brightness, and Now Playing layout/background
- Guided first installation, firmware updates and repair for EchoDesk Mini
- Touchscreen-originated active-app and settings updates
- System tray, minimise-to-tray, current-user autostart and desktop appearance

The UI leaves settings disabled when the firmware does not advertise support;
it never simulates a local-only change as a successful device write.

## Protocol safety

- UTF-8 newline JSON, protocol v1, maximum 65,536 bytes
- Allowlisted methods only and bounded command timeouts
- Monotonic broker and device-settings revisions
- Device and component acknowledgements drive UI state; commands are not applied
  optimistically
- The bundled provider is version- and SHA-256-pinned during packaging

## Build and package

From the repository root:

```powershell
& .\desktop\build.ps1
```

The script first validates the merged ESP32 image and pinned release manifest.
It then builds and tests the WPF solution, builds the hidden Rust host (with its
in-process espflash 4.6.0 engine), builds the performance helper, verifies the
CLI-only provider, and produces a self-contained `win-x64` ZIP plus SHA-256
file under `desktop/dist/`. Advanced build agents may pass explicit
`-HostExecutable` or `-CodexBarCli` paths; each is still validated. Release
builders use `-RequireInstaller` so a missing NSIS compiler cannot accidentally
produce a release without the per-user setup executable.
