# EchoDesk Desktop for macOS

EchoDesk Desktop 0.2.10 is a native AppKit application for macOS 14 or later.
The same application bundle runs natively on Apple silicon and Intel Macs. It
uses the existing EchoDesk serial protocol and keeps USB as the default device
connection, firmware update, and recovery path.

The normal desktop window matches the Windows product structure: Overview,
Device Control, Customisation, Setup new device, and Device recovery & updates.
On first launch, a local two-screen welcome asks whether to set up the first
EchoDesk now. Choosing Yes opens the existing USB/WiFi setup flow; Not now
opens Overview. The completed welcome is remembered locally and does not
depend on a Host or connected device. There is no legal or Terms step. WiFi
setup never silently joins or alters a Mac's network.

## Build on macOS

Requirements:

- macOS 14 or later with current Xcode command-line tools
- Rust and `rustup` for the universal display-plugin helper
- Internet access to retrieve Espressif's signed universal esptool binaries
- Internet access to retrieve the pinned arm64 and x86_64 CodexBar CLI archives
- `desktop/firmware/echodesk-mini/2.33.0/echodesk-os.bin` with the pinned hash

Run:

```bash
cd companion
PRODUCT_MODE=echodesk ./build.sh
```

The build verifies the bundled EchoDesk OS before packaging it and produces:

- `build/EchoDesk Desktop.app`
- `build/EchoDesk-Desktop-v0.2.10-macos-universal.dmg`
- `build/EchoDesk-Desktop-v0.2.10-macos-universal.zip`

The EchoDesk DMG uses the checked-in 1536×1024 installer artwork, a matching
768×512 Finder icon-view window, and fixed left-to-right EchoDesk Desktop →
Applications placement. The build fails if the artwork or Finder layout cannot
be applied, preventing an unstyled installer from being released.

Without a Developer ID certificate the local build is ad-hoc signed and is
for testing only. The release workflow refuses to upload an ad-hoc build. A
manually dispatched `.github/workflows/mac-app.yml` run can build `echodesk`
and upload the notarized DMG and ZIP when the `v0.2.10` release is ready. Do
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

## Native telemetry

The macOS app collects CPU, memory and network throughput with public Darwin
APIs and sends the same Performance v1 payload used on Windows. macOS does not
provide a public system-temperature API, so temperature and GPU fields remain
unavailable rather than being fabricated.

Now Playing supports Apple Music and Spotify through their public Apple-event
dictionaries. macOS may ask for Automation permission the first time either
application is queried or controlled. Other players produce a live empty state
instead of incorrectly claiming that EchoDesk Desktop is not running.

Codex usage is provided by the pinned universal helper inside
`Contents/Helpers/CodexBarCLI`; users do not need Homebrew or a separate
CodexBar installation. Codex requests explicitly select the OAuth source.

Before a compatible Mini enters its ROM bootloader, Desktop requests a static
“Updating EchoDesk OS” warning. The request is bounded and non-fatal for legacy
or recovery firmware. After a verified update reconnects at the expected
version, Desktop asks the new firmware to show the short completion screen.
