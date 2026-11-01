# EchoDesk guided device setup

EchoDesk Desktop 0.2.6 and EchoDesk OS 2.31.0 implement a coordinated first-run
flow for the Freenove FNK0114A_2P4 EchoDesk Mini. The WPF app never opens a COM
port; the bundled EchoDesk Host remains the sole serial owner and the guided
wizard reuses the existing firmware manager and flasher.

## Blank Terms and Privacy step

The first Desktop wizard step remains labelled **Terms** so the onboarding
sequence and back-navigation remain stable. Its content is intentionally blank:
there are no placeholder terms, privacy copy, document links or acceptance
checkbox. Continue is enabled and advances to the USB/Wi-Fi connection choice.
This avoids presenting invented legal text while keeping the requested
onboarding step available for reviewed copy in a later release.

## Customer flow

1. A blank device shows **Hello.**, then language selection and the Desktop
   download prompt. English is persisted; the production download URL remains
   intentionally unset until `ECHODESK_SETUP_URL` is configured with HTTPS.
2. Desktop passes through the blank Terms/Privacy step, then the device offers
   USB (the default) or **Setup with Wi-Fi**. USB uses the continuously
   refreshed serial-device list and a proven EchoDesk handshake.
3. An unproven supported USB serial adapter is labelled unidentified rather
   than EchoDesk. The customer must confirm the physical FNK0114A_2P4 label;
   the existing updater then positively probes an original ESP32 with 4 MB
   flash before the device can be selected.
4. The host pins the selected port, model and confirmation, starts a setup
   session, and invokes the existing `firmware_install` operation in `install`
   mode. Manifest, merged-image, SHA-256, chip, flash layout, write and
   post-restart checks are unchanged.
5. Desktop reports only host-supplied stages and measured percentages. A flash
   disconnect is expected. Completion requires the write to finish, the same
   port to reconnect as a proven EchoDesk Mini on the expected firmware, and a
   version-1 setup acknowledgement.
6. EchoDesk OS persists `completed` and opens Home. Factory Reset from either
   physical Settings or Desktop Device Control clears only onboarding state
   and language, retains other preferences and firmware, and returns to Hello.

For optional Wi-Fi setup, the Mini creates a WPA2-protected temporary
`EchoDesk-XXXX` network for 15 minutes and shows its random 8-digit code.
Windows joins that network manually; Desktop discovers only the fixed local
endpoint and provisions the home SSID/password. The Mini acknowledges the
bounded request, restarts, joins the home network and can then be rediscovered
on the LAN. This phase does not move Codex usage or device control to Wi-Fi;
USB remains the primary runtime, firmware-update and recovery transport. See
[`wifi-setup-protocol.md`](wifi-setup-protocol.md).

## Persisted setup states

`notConfigured`, `waitingForDesktop`, `inProgress`, and `completed` are stored
in the existing NVS preferences namespace with setup protocol version 1. An
upgrade that finds prior EchoDesk preference keys but no setup key treats the
device as completed so existing owners are not forced through onboarding.
Blank NVS remains not configured.

Setup begin and completion are safe for an already-completed device and never
regress it. A newly flashed blank device may accept the verified completion
command directly after reconnect because the pre-flash session cannot survive
a full merged-image installation.

## Broker and serial contract

Desktop-to-host methods are bounded protocol-v1 requests:

```json
{"method":"setup_begin","params":{"port":"COM7"}}
{"method":"setup_complete","params":{"port":"COM7"}}
{"method":"factory_reset","params":{"port":"COM7"}}
```

The host maps them onto the existing serial exchange:

```json
{"cmd":"echodesk_setup_begin","version":1,"requestId":101}
{"cmd":"echodesk_setup_complete","version":1,"requestId":102}
{"cmd":"echodesk_factory_reset","version":1,"requestId":103}
```

State includes `setupState`, `setupVersion`, and an optional HTTPS-only
`setupUrl` of at most 256 characters. Old firmware remains valid when both
setup fields are absent. A partial pair, unknown state, unsupported version or
unsafe URL is rejected.

## Recovery and validation boundary

Disconnect, busy-port, checksum, probe, write, verification, reconnect and
cancel failures remain in the existing firmware manager and retain its safe
retry/recovery behavior. The wizard never follows a different port after its
selection is pinned and never declares success from a write alone.

Automated coverage exercises the blank introductory step, multiple-device selection,
bootloader confirmation, setup handshakes, pinning, verified completion,
factory reset, protocol validation, persistence and native state transitions.
Build validation does not constitute a physical flash or touchscreen test.
