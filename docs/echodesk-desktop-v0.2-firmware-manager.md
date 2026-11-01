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

No public release URL has been invented. When official GitHub Releases are
published, the HTTPS asset URL can be added through the release process. A
remote production manifest must be authenticated independently of the checksum
inside that same manifest. The package's Windows code signature is the trust
root for the bundled manifest; remote updates require a separately pinned
manifest signing key. The manager rejects unsigned remote manifests.

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
`EchoDesk-Desktop-v0.2.5-win-x64.zip`.

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

1. Build `freenove-fnk0114a-2p4` and the merged image with
   `scripts/build_freenove_firmware.ps1`.
2. Update the release record's version, size, SHA-256 and notes.
3. Regenerate `firmware-manifest.json.sha256` from the final manifest.
4. Run the firmware packaging tests and full desktop tests.
5. Build the self-contained desktop package and verify its ZIP checksum.
6. Code-sign the Windows binaries and installer for production distribution.
7. Publish GitHub Release assets only with the repository owner's permission.

This implementation does not publish a release, deploy a website or write any
physical device as part of its build and test process.
