# EchoDesk Desktop for macOS

EchoDesk Desktop 0.2.7 is a native AppKit application for macOS 14 or later.
The same application bundle runs natively on Apple silicon and Intel Macs. It
uses the existing EchoDesk serial protocol and keeps USB as the default device
connection, firmware update, and recovery path.

The normal desktop window matches the Windows product structure: Overview,
Device Control, Customisation, Setup new device, and Device recovery & updates.
Setup starts directly at the USB/WiFi connection choice; there is no legal or
Terms step. WiFi setup never silently joins or alters a Mac's network.

## Build on macOS

Requirements:

- macOS 14 or later with current Xcode command-line tools
- Rust and `rustup` for the universal display-plugin helper
- Internet access to retrieve Espressif's signed universal esptool binaries
- `desktop/firmware/echodesk-mini/2.31.0/echodesk-os.bin` with the pinned hash

Run:

```bash
cd companion
PRODUCT_MODE=echodesk ./build.sh
```

The build verifies the bundled EchoDesk OS before packaging it and produces:

- `build/EchoDesk Desktop.app`
- `build/EchoDesk-Desktop-v0.2.7-macos-universal.dmg`
- `build/EchoDesk-Desktop-v0.2.7-macos-universal.zip`

Without a Developer ID certificate the local build is ad-hoc signed and is
for testing only. The release workflow refuses to upload an ad-hoc build. A
manually dispatched `.github/workflows/mac-app.yml` run can build `echodesk`
and upload the notarized DMG and ZIP when the `v0.2.7` release is ready. Do
not enable release upload while the release remains under development.

## Wi-Fi setup

On the EchoDesk Mini, choose **Setup with Wi-Fi**, then manually join the
temporary `EchoDesk-XXXX` network from macOS using the displayed eight-digit
code. In EchoDesk Desktop, open **Setup new device**, choose **Setup with
WiFi**, select **Find WiFi devices**, and provide the same code plus the
destination Wi-Fi credentials.

Discovery accepts only the EchoDesk v1 protocol and the Freenove
`FNK0114A_2P4` model. Provisioning targets only a freshly discovered setup-mode
device at `192.168.4.1`; paired LAN devices must authenticate with a random
credential held in the macOS Keychain. Passwords are not persisted by the
desktop app.

Firmware is never flashed automatically by the build or onboarding flow.
