# Feishin Windows media-session research

Research date: 2026-10-30

Scope: Feishin's Windows GSMTC/SMTC behavior and a credential-safe optional integration for EchoDesk. Sources are limited to Feishin's repository, releases and issue tracker, Microsoft's Windows API documentation, and the W3C Media Session specification material.

## Decision summary

- **Use Windows GSMTC as the default integration.** Feishin 1.17.0 does publish a media session with title, artist, album, artwork, playback state and playback actions. This is not merely planned functionality; it is present in the released source.
- **Feishin GSMTC has an important configuration boundary:** it works only with Feishin's **Web** player backend. On desktop, the user must enable **Settings → Hotkeys → Enable media session** and restart Feishin. The setting is disabled for the MPV backend.
- **Treat timeline data and stopped state as optional/unreliable for Feishin.** Feishin does not explicitly publish Media Session position/duration, and its current state mapping reports every non-playing state—including stopped—as paused.
- **Add an opt-in Feishin Remote adapter in the Windows companion as the fallback**, not on the ESP32. Feishin's existing Remote WebSocket exposes live song, status and position updates and accepts play, pause, previous and next commands. This avoids a Jellyfin login in EchoDesk and does not require changing music player.
- **Never send Feishin/Jellyfin URLs, IDs, credentials, or raw Remote payloads to the ESP32.** The companion should allowlist metadata fields, fetch/decode artwork on Windows, and send only bounded normalized metadata plus compressed image bytes over the existing serial owner.

## What Feishin actually exposes

The current stable release at the time of research is [Feishin 1.17.0](https://github.com/jeffvli/feishin/releases/tag/v1.17.0), commit `5c169aca8d550032c9aa6e8e2b9f624d03d42fec`. The relevant Media Session and Remote source blobs in the current `development` commit inspected (`8d12eae8a1551364442461bac02d99d78c185820`) are identical to the 1.17.0 release blobs.

Feishin implements the browser/Electron `navigator.mediaSession` API. The released hook:

- enables the desktop media session only when the user setting is on **and** `playbackType === WEB` ([source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/renderer/features/player/hooks/use-media-session.ts#L68-L75));
- registers `play`, `pause`, `previoustrack`, `nexttrack`, `stop`, and seek handlers ([source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/renderer/features/player/hooks/use-media-session.ts#L81-L170));
- publishes title, artist, album, and a single artwork URL for ordinary tracks ([source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/renderer/features/player/hooks/use-media-session.ts#L173-L215)); and
- publishes `playing` for Feishin's playing state and `paused` for every other state ([source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/renderer/features/player/hooks/use-media-session.ts#L244-L274)).

The settings UI confirms the operational requirement: the switch is disabled on Linux, in the web build, and whenever the desktop player backend is not Web; changing it shows a restart-required notification ([source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/renderer/features/settings/components/hotkeys/media-session-settings.tsx#L18-L64)). Feishin's maintainer also instructed a user to enable this setting and restart, after which media keys worked ([issue #1557](https://github.com/jeffvli/feishin/issues/1557#issuecomment-3752562243)).

This supersedes the conclusion one might draw from April 2026 feature request [#1936](https://github.com/jeffvli/feishin/issues/1936), whose reporter said SMTC was absent. The released source is the stronger evidence. Feishin's [1.10.0 release notes](https://github.com/jeffvli/feishin/releases/tag/v1.10.0) also explicitly mention a fix preventing media-session handlers from breaking or vanishing.

### Known limitations in release 1.17.0

1. **No MPV-backend GSMTC session.** The source deliberately gates Media Session to `PlayerType.WEB`; this is not a detection bug in EchoDesk.

2. **Position and duration are not explicitly supplied.** The W3C API provides `setPositionState({ duration, playbackRate, position })` for this purpose ([Media Session explainer](https://github.com/w3c/mediasession/blob/main/explainer.md#the-mediapositionstate-dictionary)), but Feishin 1.17.0 contains no `setPositionState` call. Chromium may infer a timeline from its media element in some circumstances, but EchoDesk must not depend on that. A missing or zero GSMTC timeline is therefore valid and must not be fabricated.

3. **Stopped is indistinguishable from paused through Feishin's published Media Session state.** Feishin's own status enum has distinct `paused`, `playing`, and `stopped` values ([source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/shared/types/types.ts#L149-L153)), but the Media Session hook maps only `playing` specially and maps everything else to `paused`. When the song becomes undefined, its metadata callback returns without clearing metadata ([source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/renderer/features/player/hooks/use-media-session.ts#L173-L211)). EchoDesk needs stale-data expiry rather than assuming paused metadata remains current forever.

4. **A current Windows edge case can disable controls.** Open bug [#2650](https://github.com/jeffvli/feishin/issues/2650) reproduces on Feishin 1.17.0/Windows 11: skipping to next or previous while paused can make Windows media controls stop working until playback is resumed inside Feishin. EchoDesk must confirm state after every command and time out without optimistic state changes.

5. **Radio capability reporting is imperfect.** Feishin registers previous and next handlers, but those handlers return without action while Internet Radio is active and playing ([source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/renderer/features/player/hooks/use-media-session.ts#L98-L120)). Windows may therefore expose controls that Feishin will ignore.

6. **Artwork is best-effort.** Feishin publishes one image URL and labels it `image/png` without declaring dimensions. The companion must decode by actual bytes, enforce input/output size limits, and tolerate a null or unreadable GSMTC thumbnail.

## GSMTC implications for EchoDesk

Microsoft's `GlobalSystemMediaTransportControlsSessionManager` exposes all integrated sessions, the session Windows currently considers most likely, and `SessionsChanged`/`CurrentSessionChanged` events ([Microsoft documentation](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager?view=winrt-26100)). It requires Windows 10 version 1809 or newer and the `globalMediaControl` capability.

Each session exposes:

- application identity through `SourceAppUserModelId`;
- media properties through `TryGetMediaPropertiesAsync`, including title, artist, album title and thumbnail;
- playback and timeline snapshots plus `MediaPropertiesChanged`, `PlaybackInfoChanged`, and `TimelinePropertiesChanged` events; and
- asynchronous play, pause, previous, and next requests ([session API](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssession?view=winrt-26100)).

The thumbnail is an `IRandomAccessStreamReference`, not a guaranteed file path or URL ([Microsoft documentation](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmediaproperties.thumbnail?view=winrt-26100)). Timeline `Position` is only current as of `LastUpdatedTime`, and `EndTime` is the available end timestamp ([Microsoft documentation](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessiontimelineproperties?view=winrt-26100)). EchoDesk should extrapolate position locally only while confirmed playing, clamp it to a valid duration, and resynchronize on events and the approximately one-second poll.

The playback-controls object advertises `IsPlayEnabled`, `IsPauseEnabled`, `IsPreviousEnabled`, and `IsNextEnabled` ([Microsoft documentation](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionplaybackcontrols?view=winrt-26100)). Those flags should drive the ESP32 button enabled states. However, because Feishin's radio handlers can no-op, successful capability advertisement is not proof the command changed playback.

The Windows command APIs are described as attempts to request a change and return a Boolean request result (for example, [`TryPlayAsync`](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssession.tryplayasync?view=winrt-26100)). Therefore, the authoritative UI update must come from a subsequent playback/timeline event or fresh snapshot, not from the button press or returned `true` alone.

For multiple sessions, use this deterministic policy:

1. retain a selected session while it remains playing and present;
2. otherwise prefer sessions whose confirmed status is `Playing`;
3. use the manager's current session as a tie-breaker, then a stable application/session ordering;
4. apply a short switch hold-down so transient browser sessions do not cause rapid changes; and
5. never replace a playing music session with a paused browser tab.

This builds on Windows' own current-session hint without assuming that hint is always the user's desired music player. For browser sessions, the browser/user agent decides which page's Media Session is routed to the platform ([W3C routing notes](https://github.com/w3c/mediasession/blob/main/explainer.md#mediasession-routing)), so EchoDesk can reliably identify the browser application but cannot always identify the originating tab/site.

## Optional Feishin Remote adapter

When Feishin uses MPV, Media Session is disabled, or the current Windows session becomes stale, the best player-specific fallback is Feishin's built-in Remote server.

The released source shows that Remote is disabled by default, uses port `4333`, and supports optional username/password settings ([server defaults](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/main/features/core/remote/index.ts#L46-L66)). Its desktop settings expose enable, port, username, and password controls ([source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/renderer/features/settings/components/window/remote-settings.tsx#L16-L149)). Authenticated WebSocket clients receive an initial cached player state, then incremental updates ([source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/main/features/core/remote/index.ts#L145-L180)). Feishin broadcasts song, `playing`/`paused`/`stopped`, and position changes ([server source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/main/features/core/remote/index.ts#L884-L909), [position source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/main/features/core/remote/index.ts#L1004-L1007)). The song object includes duration, title, artist, album and image URL fields ([song type](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/shared/types/domain-types.ts#L394-L455)). Simple JSON events `play`, `pause`, `previous`, and `next` are accepted ([protocol type](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/shared/types/remote-types.ts#L140-L142), [server dispatch](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/main/features/core/remote/index.ts#L566-L576)).

Recommended adapter behavior:

- Make it explicitly opt-in and configurable in the Windows companion; never auto-scan the LAN.
- Connect to loopback (`127.0.0.1`/`localhost`) only. Keep the Feishin Remote username/password in the Windows credential store or existing companion configuration, never in firmware or serial messages.
- Warn that Feishin serves plain HTTP/WebSocket and calls `server.listen(port)` without a loopback host ([source](https://github.com/jeffvli/feishin/blob/5c169aca8d550032c9aa6e8e2b9f624d03d42fec/src/main/features/core/remote/index.ts#L375-L470)); enabling it may expose the service on local network interfaces. Username/password authentication is preferable to blank credentials but is not transport encryption.
- Parse bounded JSON and allowlist only `name`, `artistName`, `album`, `duration`, `status`, `position`, and a transient artwork source. Discard server IDs and all other fields.
- Fetch artwork in the companion, validate/decode it, resize/compress it, hash the final bytes, and send them only when the hash changes. Never forward the Remote `imageUrl` to the ESP32.
- Use Remote's `radio-status` to disable previous/next for radio. Outside radio, Remote has no general command-capability negotiation, so its enabled-state fidelity is weaker than GSMTC's playback-control flags.
- Prefer GSMTC whenever a healthy Feishin session is available. Activate Remote only after a short GSMTC-missing/stale threshold, and switch back with hysteresis. Remote Feishin data must not override another application's actively playing GSMTC session.
- After sending a Remote control event, wait for the subsequent status/song/position broadcast. Simple playback controls do not carry operation acknowledgements; the changed state is the confirmation.

## Why not integrate directly with Jellyfin

The Feishin Remote fallback is player-local: it reports the exact Feishin queue item and player state regardless of whether the library came from Jellyfin, Navidrome, or Subsonic. A direct Jellyfin integration would add a second authentication and session-selection problem and still would not reliably identify which Windows application the user intends EchoDesk to control.

Accordingly, Part 1 should not request Jellyfin credentials at all. If neither GSMTC nor the optional Feishin Remote is usable, EchoDesk should show its polished no-session/stale state rather than requesting a Jellyfin token or inventing metadata. This also guarantees that no Jellyfin password or token can reach the ESP32.

## Physical validation still required

The implementation should be exercised on the target Windows machine with:

- Feishin 1.17.0 Web backend, Media Session off and on (with restart);
- Feishin Web backend play/pause/previous/next, including skip-while-paused;
- Feishin MPV backend with GSMTC absent and Remote fallback enabled;
- authenticated and unauthenticated Remote configurations, including reconnects;
- tracks with missing album/artist/artwork and unusually large/corrupt artwork;
- Feishin plus an actively playing browser or Spotify session; and
- radio playback, verifying previous/next are disabled in the EchoDesk UI.

These checks cannot be proven from source alone because Electron/Chromium-to-Windows SMTC behavior, installed Feishin configuration, Windows session arbitration, and the actual USB/ESP32 memory path are runtime-specific.
