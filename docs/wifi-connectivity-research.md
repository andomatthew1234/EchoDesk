# Wi-Fi connectivity research — Freenove FNK0114A_2P4

**Scope:** read-only assessment on 1 November 2026. No firmware, Desktop, or
device configuration was changed.

## Finding

The FNK0114A_2P4 can use Wi-Fi. Freenove identifies the product's module as an
ESP32-WROOM-32E with its built-in PCB antenna, and its official tutorial ships a
Wi-Fi station/client example that joins a **2.4 GHz** network and serves HTTP on
port 80. This is evidence that the board hardware can join a LAN and exchange
IP traffic; it is not evidence of 5 GHz support or that EchoDesk currently
offers a LAN transport.

Primary sources:

- [Freenove Preface: ESP32-WROOM-32E and built-in PCB antenna](https://docs.freenove.com/projects/fnk0103/en/latest/fnk0103/codes/tutorial/Preface.html#esp32-wroom)
- [Freenove Wi-Fi Web Server tutorial: 2.4 GHz requirement and IP-based access](https://docs.freenove.com/projects/fnk0103/en/latest/fnk0103/codes/tutorial/9_WIFI_Web_Server.html)
- [Freenove's official `WiFiServer` example at commit `6da6dae`](https://github.com/Freenove/Freenove_ESP32_Display/blob/6da6dae20f2d0b0bb85854691ac52dc5479f5c51/Sketches/Sketch_09.1_WiFi_Web_Servers_LED/Sketch_09.1_WiFi_Web_Servers_LED.ino)
- [Freenove GPIO table](https://docs.freenove.com/projects/fnk0103/en/latest/fnk0103/codes/tutorial/Preface.html#gpio-pinout-table)

The radio is internal to the ESP32-WROOM module. It needs no additional GPIO
connection, but the physical board's display/touch hardware already occupies
the documented pins: LCD SPI GPIO12/13/14/15 and GPIO2, plus the separate
touch bus used by EchoDesk. A Wi-Fi transport must therefore preserve the
existing display and touch driver configuration rather than repurposing pins.

## What the present EchoDesk firmware already does

The repository already contains a narrow Wi-Fi feature, independent of the
newer desktop guided setup work:

- `src/wifi_time.cpp` calls `WiFi.mode(WIFI_STA)`, joins a stored SSID, disables
  Wi-Fi sleep, reconnects every 30 seconds, and configures NTP from
  `pool.ntp.org` and `time.nist.gov`.
- `src/main.cpp` initializes and ticks that service in the normal Freenove
  firmware loop.
- The existing USB JSON protocol accepts `wifi_scan`, `wifi_status`,
  `wifi_set`, and `wifi_forget` in `src/serial_receiver.cpp`.
- Scan output is capped at 16 networks. `wifi_set` persists an SSID and password
  in the existing ESP32 Preferences/NVS namespace, attempts connection for up
  to ten seconds, then returns the status over USB serial.
- On a fresh device with no saved credentials, the radio stays off. The feature
  was deliberately designed for standalone clock/NTP use; usage data remains
  USB-only, as stated in `src/wifi_time.cpp`.

The source does **not** create an HTTP server, TCP server, UDP listener,
WebSocket, MQTT client, mDNS advertiser, or outbound EchoDesk-host connection.
Consequently, Wi-Fi currently provides internet time and a Wi-Fi status icon,
not an alternative connection to EchoDesk Desktop.

The legacy Tauri companion still contains a USB-only Wi-Fi setup view and
serial commands, but current EchoDesk Desktop 0.2 WPF has no corresponding
broker method or user interface. Neither version communicates with the device
over Wi-Fi today.

## How a future Desktop connection could work

This is an architectural inference, not an implemented plan:

1. Keep USB as the bootstrap/recovery path. Use its existing bounded JSON
   protocol to provision Wi-Fi and establish a one-time pairing secret.
2. Let the device make an authenticated outbound LAN connection to the
   Windows-hosted companion service, or use a deliberately chosen local broker.
   This avoids exposing an unauthenticated ESP32 HTTP server to every LAN peer.
3. Bind any Windows listener to the local/private network deliberately, use
   authenticated and versioned frames, rate/size limits, reconnect backoff and
   a visible paired-device identity. A firewall prompt and discovery mechanism
   (for example mDNS) would need explicit product design and testing.
4. Retain USB fallback for firmware flashing, Wi-Fi recovery, factory reset and
   failure diagnosis. Wi-Fi must not silently select a different device.

The existing ESP32 has limited RAM/flash headroom and the display/touch work is
latency-sensitive. A future implementation should measure coexistence under
screen redraw, touch input, NTP and usage updates before making Wi-Fi the
default transport. It should also avoid storing credentials in plaintext NVS
where a stronger provisioning/pairing design is feasible.

## Bottom line

| Question | Answer |
| --- | --- |
| Can the Freenove board join Wi-Fi? | Yes, on 2.4 GHz Wi-Fi, using its ESP32-WROOM-32E PCB antenna. |
| Can EchoDesk currently configure Wi-Fi? | The firmware supports USB-serial setup/status commands; the legacy Tauri UI exposed them, the current WPF Desktop does not. |
| Does EchoDesk Desktop currently connect to the device over Wi-Fi? | No. The current product path is CH340 USB serial at 115200 baud. |
| Is Wi-Fi viable as a future app transport? | Yes in principle, but it needs a new authenticated LAN protocol, discovery/pairing, Windows firewall behavior, recovery design and hardware performance testing. |
