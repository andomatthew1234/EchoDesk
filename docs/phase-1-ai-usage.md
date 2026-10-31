# EchoDesk Phase 1: AI Usage architecture

This document records the AI Usage path before and after Phase 1. Phase 1 makes
EchoDesk Desktop the normal Windows companion while preserving the existing
firmware wire format. It deliberately does **not** add first-boot, welcome, or
factory-reset behavior.

## Architecture before Phase 1

The original Windows flow had two visible applications:

1. Win-CodexBar obtained Codex limits using the user's existing supported local
   Codex authentication and exposed them through `codexbar-cli.exe`.
2. AI Monitor invoked that CLI every 180 seconds, normalized its snake-case
   response, and built the existing schema-1 usage envelope.
3. AI Monitor alone discovered and opened the ESP32 serial port at 115200 baud.
   It also sent Performance, Now Playing, settings, and device-control traffic.
4. EchoDesk Desktop v0.1 did not open a COM port. It sent allowlisted controls
   to AI Monitor over the per-user `EchoDesk.Companion.v1` named pipe.
5. The firmware's serial receiver parsed the usage envelope into the Codex view.

AI Monitor was therefore still required for much more than Codex polling: it
was the serial owner and the telemetry host for all desktop-fed components.
Running another serial client in parallel would contend for the same COM port.

## Architecture after Phase 1

EchoDesk Desktop now owns the complete companion lifecycle. Its packaged,
headless EchoDesk Host is an internal implementation component, not a separate
application the user installs or launches. The desktop starts and monitors that
host, communicates with it over the existing bounded named-pipe API, and shows
AI Usage status and controls in the EchoDesk UI.

```text
EchoDesk Desktop (WPF UI, component settings, host lifecycle)
        |
        | per-user named pipe; no COM access
        v
EchoDesk Host (headless, one process)
        |-- Codex poll worker --> bundled codexbar-cli.exe
        |
        `-- sole 115200-baud serial owner --> EchoDesk firmware
```

The headless host remains the sole serial owner for AI Usage, Performance, Now
Playing, settings, and control frames. EchoDesk Desktop must not add a direct
COM fallback; that would recreate competing-port access. The host is monitored
and restarted by the desktop and reconnects after USB removal and Windows
sleep/wake through the existing serial service.

The Codex worker:

- runs once when enabled and then on the existing 180-second interval;
- invokes the supported headless CLI without opening the Win-CodexBar GUI;
- uses the user's existing Codex login and never asks for, copies, or scrapes a
  token;
- maps a real 5-hour window, 7-day window, percentages, and reset timestamps;
- omits informational windows such as “No active 5h session” instead of
  fabricating a zero-percent session;
- reports fetching, authentication, provider, and unavailable-data states;
- performs the blocking CLI call off the UI thread and does no busy polling.

The desktop exposes this as the **AI Usage Monitor** component. Its enabled
state is persisted. Disabling it invalidates an in-flight result, stops future
Codex polls, and prevents AI usage frames without disabling other components.
The component reports its current status, last successful update, and actionable
error while the shared serial service continues to run.

## Firmware protocol compatibility

Phase 1 does not introduce a second protocol. EchoDesk Host continues using the
existing `AIM1` framed transport and JSON `schemaVersion: 1` envelope at 115200
baud. The provider wire key remains `codex`. A usage frame remains authoritative:
if `primary` is omitted, the previous 5-hour window is cleared; if a notice is
sent, prior percentages are not presented as current data.

The firmware continues to provide:

- the 5-hour percentage and reset countdown while a session is active;
- the weekly percentage and reset countdown;
- used/remaining presentation from `percentMode`;
- omission of the 5-hour card when both its reset and window length are absent;
- touch and Home navigation.

## Device display-state policy

The Codex screen uses a small pure classifier rather than treating every cached
`valid` bit as live. This is display policy only; parsing and schema-1 storage
remain unchanged.

| State | Device behavior |
| --- | --- |
| Never connected | Hide bars, show `OPEN ECHODESK DESKTOP TO ENABLE AI USAGE` and a waiting spinner |
| Waiting/fetching | Hide bars, show `WAITING FOR CODEX DATA` and a spinner |
| Authentication required | Hide bars, show `SIGN IN TO CODEX IN ECHODESK DESKTOP` |
| Current host notice | Hide bars and show the bounded notice text as an attention state |
| Live sample under five minutes old | Show the real session/weekly cards |
| Sample five minutes old or older | Hide bars and show `RECONNECT ECHODESK DESKTOP` |

The age calculation uses unsigned `millis()` subtraction, so the five-minute
boundary remains correct across the ESP32 counter rollover. A current notice is
classified before the retained timestamp: authentication or provider attention
must not be mislabeled as stale usage. The host still owns longer-term status
and refresh timing.

## Dependencies, licensing, and attribution

- EchoDesk and the inherited ESP32 AI Monitor code are MIT licensed; the root
  `LICENSE` notice remains applicable.
- [Win-CodexBar](https://github.com/nesszer/Win-CodexBar) is MIT licensed. Phase
  1 reuses its supported headless CLI and mapping rather than reimplementing its
  Codex retrieval. Its required licence/attribution must ship with any bundled
  binary.
- Codex authentication remains an external user account/login dependency. The
  CLI is managed transparently inside the EchoDesk package, but a valid Codex
  login is still required for live data.
- Windows 11 and the packaged EchoDesk Desktop/Host runtime remain runtime
  dependencies. A separately installed or visible AI Monitor or Win-CodexBar
  application is not part of the normal Phase 1 user experience.

## Validation boundaries

Native firmware tests cover never-connected, waiting, authentication, current
notice, live, exact five-minute expiry, counter rollover, and inactive-window
classification. The Freenove firmware target must still compile as the final
integration check. No firmware is flashed as part of Phase 1, and a successful
desktop or firmware build is not evidence of a physical display/USB test.
