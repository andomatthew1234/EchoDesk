# EchoDesk Desktop v0.2 firmware manager

EchoDesk Desktop v0.2 adds guided installation, update and repair for the
initial supported product, **EchoDesk Mini**. EchoDesk Mini is the Freenove
FNK0114A_2P4 with the original ESP32, ST7789 display, XPT2046 touch controller
and CH340 USB serial bridge.

## Customer installation model

The self-contained Windows package includes all executable code and firmware
needed for an offline installation:

- `EchoDesk Desktop.exe` provides the guided Setup & Updates interface.
- `EchoDesk Host.exe` remains the only serial-port owner and contains the
  pinned espflash 4.6.0 library.
- `firmware/firmware-manifest.json` describes the supported release.
- `firmware/firmware-manifest.json.sha256` pins the exact bundled manifest.
- `firmware/echodesk-mini/2.31.0/echodesk-os.bin` is the merged ESP32 image for
  address `0x0`.

Customers do not install Python, PlatformIO or a separate esptool program.
Windows must already expose the USB serial adapter as a COM port. The
application never uses AI Monitor's stock firmware updater.

## Release manifest version 1

The checked-in manifest is
[`desktop/firmware/firmware-manifest.json`](../desktop/firmware/firmware-manifest.json).
Its top-level fields are `schemaVersion`, `generatedAt` and `releases`. Each
release contains:

| Field | Meaning |
| --- | --- |
| `firmwareVersion` | Semantic EchoDesk OS version. |
| `hardwareModel` / `hardwareId` | Human and stable device-specific identity. |
| `supportedChip` | Bootloader chip family that must be detected before a write. |
| `imageType` | `merged` for the complete flash image. |
| `flashAddress` | Numeric destination address; `0` for this merged image. |
| `flashMode`, `flashFrequency`, `flashSize` | Values used to build and validate the image. |
| `imageFile` | Safe relative path inside the desktop package. |
| `downloadUrl` | HTTPS release asset, or `null` for the bundled release. |
| `sha256`, `size` | Exact content identity and byte length. |
| `releaseNotes` | Text shown before an update. |
| `minimumDesktopVersion` | Oldest manager allowed to install the release. |

Remote releases use dedicated `fw-vX.Y.Z` stable tags and
`fw-beta-vX.Y.Z` prerelease tags. Each GitHub firmware release contains the
versioned EchoDesk Mini merged image, its SHA-256 sidecar, the exact
`firmware-update-manifest.json` bytes and a detached Ed25519 signature. The
download URL in that signed manifest is confined to the matching tag in the
`andomatthew1234/EchoDesk` repository. A checksum inside an unsigned manifest
is never accepted as authentication.

The package's Windows or macOS application signature remains the trust root
for the offline bundled recovery image. Remote manifests use a separately
pinned Ed25519 public key. The release workflow derives the public key from
the private GitHub Actions secret and refuses to sign unless its SPKI SHA-256
fingerprint is exactly
`f626ed638e7d4fc155ef834a735b7866ab974aef586b399936247915ffe95ca1`.
The private key is never stored in the repository or in a build artifact.

## Packaging safeguards

Run the packaging validation directly with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test_echodesk_firmware_package.ps1
```

`scripts/package_echodesk_firmware.ps1` rejects:

- a missing or modified manifest digest;
- a missing, truncated, oversized or hash-mismatched image;
- an image without the original-ESP32 bootloader at `0x1000`;
- an image without the partition table at `0x8000` or app at `0x10000`;
- any chip, board, flash address, mode, frequency or size outside the EchoDesk
  Mini allowlist;
- unsafe relative paths and non-HTTPS remote URLs; and
- manifest versions that do not match `src/config.h`.

The packager copies firmware only after every check passes, then hashes the
copy again. `desktop/build.ps1` places the manifest digest, firmware digest and
in-process flash-engine version into `component-manifest.json` before creating
`EchoDesk-Desktop-v0.2.6-win-x64.zip`.

`desktop/installer/EchoDesk-Desktop.nsi` creates a current-user setup executable
that installs to `%LOCALAPPDATA%\Programs\EchoDesk Desktop`, adds Start menu
shortcuts and registers a normal Windows uninstaller. Release builders pass
`-RequireInstaller` to `desktop/build.ps1`, so a missing NSIS compiler cannot
silently produce a release without the setup executable.

## Port ownership and recovery safety

The WPF interface never opens a COM port. The host pauses telemetry and pending
controls, closes the normal 115200-baud connection, and confirms release before
the flashing worker starts. The worker identifies the Espressif bootloader and
requires the selected EchoDesk Mini profile when an EchoDesk handshake is not
available. A CH340 VID/PID alone is never treated as proof of the board model.

The merged image is written without a whole-flash erase, preserving NVS device
settings where the existing partition layout permits it. A repair warns that
settings may be reset and does not loop indefinitely after a failure. Success
is reported only after the write verifies, the device restarts, the port is
rediscovered, and the EchoDesk handshake reports the installed version.

Manual Recovery is a separate explicit escape hatch for a selected scanned COM
port. It bypasses USB-adapter identity, EchoDesk handshake, board model,
reported flash capacity and installed-version gates. It does not accept a file
path, flash offset or arbitrary image over IPC. The bundled manifest and image
remain pinned and validated, and the host must positively identify the ROM
bootloader as an original ESP32 before the first write.

## Release procedure

1. Set `APP_VERSION` in `src/config.h`, build and test the firmware and Desktop
   clients, and merge the change to the protected release branch.
2. Ensure the protected `firmware-release` GitHub Environment contains the
   base64-encoded private PEM in the
   `ECHODESK_FIRMWARE_SIGNING_KEY_PEM_B64` secret. Require an environment
   reviewer for production.
3. Create and push a `fw-vX.Y.Z` tag for stable firmware or
   `fw-beta-vX.Y.Z` for a beta. The tag version must exactly equal
   `APP_VERSION` or CI fails before publication.
4. `.github/workflows/build.yml` builds the real
   `freenove-fnk0114a-2p4` PlatformIO target with
   `scripts/build_echodesk_firmware.sh`, merges the bootloader at `0x1000`,
   partition table at `0x8000`, `boot_app0` at `0xE000` and app at `0x10000`,
   and validates the original-ESP32/DIO/40 MHz/4 MB layout.
5. The protected release job creates the manifest, verifies the signing-key
   fingerprint, signs the exact JSON bytes, verifies the signature and a
   tamper-negative test, then publishes all four update assets to GitHub.

Branch builds have read-only repository permission. Only the tag-gated,
environment-protected release job receives `contents: write`. Building or
testing never writes a physical device.
