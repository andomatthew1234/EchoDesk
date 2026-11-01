# EchoDesk guided device setup

EchoDesk Desktop 0.2.8 and EchoDesk OS 2.32.0 implement a coordinated first-run
flow for the Freenove FNK0114A_2P4 EchoDesk Mini. The WPF app never opens a COM
port; the bundled EchoDesk Host remains the sole serial owner and the guided
wizard reuses the existing firmware manager and flasher.

The Desktop wizard begins at **Connect**. There is no hidden or blank legal
step. Its active-step indicator follows Connect, Device, Options, Update,
Settings and Ready.

## Customer flow

1. A blank device shows **Hello.**, then language selection and the Desktop
   download prompt. English is persisted; the production download URL remains
   intentionally unset until `ECHODESK_SETUP_URL` is configured with HTTPS.
2. Desktop starts at the connection choice and offers USB (the default) or
   **Setup with WiFi**. USB uses the continuously
   refreshed serial-device list and a proven EchoDesk handshake.
3. Known EchoDesk devices are shown by default. An unproven supported USB serial adapter is labelled unidentified rather
   than EchoDesk. The customer must confirm the physical FNK0114A_2P4 label;
   unidentified candidates are revealed only through troubleshooting, then
   the existing updater positively probes an original ESP32 with 4 MB
   flash before the device can be selected.
4. The host pins the selected port, model and USB serial identity and starts a
   setup session. The customer may get started without a firmware write or
   check for updates and install only when a newer release is available.
5. Desktop offers Light/Dark appearance and 12/24-hour time format, then reports
   completion only from the host's authoritative setup state. During an update,
   Desktop reports only host-supplied stages and measured percentages. A flash
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

Automated coverage exercises direct Connect startup, known/unknown device filtering, multiple-device selection,
bootloader confirmation, setup handshakes, pinning, verified completion,
factory reset, protocol validation, persistence and native state transitions.
Build validation does not constitute a physical flash or touchscreen test.
