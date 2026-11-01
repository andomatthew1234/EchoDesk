# EchoDesk Wi-Fi setup protocol v1

EchoDesk OS 2.31.0 and EchoDesk Desktop 0.2.5 add opt-in Wi-Fi provisioning for
EchoDesk Mini. USB is the default connection and remains the only firmware
installation and recovery transport. This version provisions a network and
supports authenticated rediscovery; it does not carry Codex usage or device
control over Wi-Fi.

## Customer flow

1. Select **Setup with Wi-Fi** on the Mini.
2. The Mini starts `EchoDesk-XXXX` for 15 minutes and displays a random
   8-digit code. That code is also the WPA2 password.
3. Join the temporary network in Windows, then select **Find Wi-Fi devices** in
   EchoDesk Desktop.
4. Choose the discovered Freenove FNK0114A_2P4, enter the displayed code and
   the destination SSID/password, and provision it.
5. After the Mini acknowledges and restarts, reconnect Windows to the home
   network and scan again to confirm LAN reachability.

The Desktop app deliberately does not automate Windows WLAN association and
never accepts an arbitrary address or port from its UI.

## Discovery

Desktop sends one bounded UDP JSON object to broadcast and
`192.168.4.1:39320`:

```json
{"type":"echodesk_discover","version":1,"nonce":"random-safe-text"}
```

The firmware replies from port 39320 with `echodesk_device`, echoing the nonce
and reporting `deviceId`, `name`, exact physical `model`, `firmwareVersion`,
`setupState`, `setupMode`, `paired`, `ip`, and fixed service port 39321.
Desktop accepts at most 1,024 bytes, requires the reply source to equal the
declared private/link-local IPv4 address, validates every identity field and
accepts setup-mode replies only from `192.168.4.1`.

## Provisioning

Desktop can connect only to a setup-mode, unpaired device from its own scan no
more than five minutes earlier. TCP messages on port 39321 are newline-delimited
JSON and limited to 1,024 bytes:

```json
{"type":"echodesk_provision","version":1,"pairingCode":"12345678","ssid":"Home","password":"secret value","clientToken":"64-lowercase-hex-characters"}
```

The client token comes from the Windows OS cryptographic random generator.
The Mini validates exact fields and sizes, compares fixed-length secrets in
constant time, persists the token, credentials and completed state, then
returns:

```json
{"type":"echodesk_provision_result","version":1,"ok":true,"deviceId":"aabbccddeeff","restarting":true}
```

It restarts 750 ms later. Errors use the same response type with `ok:false`, a
bounded `code`, and a safe message. Passwords and tokens are never placed in
broker state or logs.

## Post-setup identity

On a completed Wi-Fi setup, UDP discovery remains available on the LAN.
Possession of the stored 64-hex client token is required for the TCP
`echodesk_identify` exchange. The token is reserved for authenticated future
Wi-Fi features; no live-data transport is enabled in this release.

The temporary AP and provisioning server exist only after an explicit on-device
choice, accept one bounded client at a time, enforce a 15-second idle timeout,
and stop after 15 minutes. Choosing USB never starts them.
