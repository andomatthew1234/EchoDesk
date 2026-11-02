# EchoDesk Desktop native macOS UI architecture

EchoDesk Desktop is already a native macOS application. Its interface is written in AppKit and its device services live in the same Swift process; it is not an Electron or web application.

## Direction

The safest migration is an incremental native-AppKit redesign rather than a full SwiftUI rewrite:

- Keep `UsageMonitor`, `SerialPortManager`, device discovery, firmware installation, recovery, settings persistence, and protocol validation unchanged.
- Keep `SettingsWindowController` as the compatibility boundary for those mature services.
- Replace dashboard-like presentation primitives with macOS window, toolbar, sidebar, form-row, sheet, alert, and semantic-colour conventions.
- Keep custom drawing only where it represents the physical 320×240 EchoDesk screen. That preview is product content, not a replacement for a native desktop control.
- Preserve narrow protocol coordinators such as PC Dashboard's revision-safe, latest-wins writer.

## Why not rewrite in SwiftUI now?

The current controller owns a large set of asynchronous serial and firmware workflows, including reconnect continuation and physical-device identity checks. Replacing all of that at once would create two competing UI/state owners and make hardware regressions difficult to isolate. SwiftUI remains suitable for future self-contained surfaces, but only after the device operations are extracted behind observable service protocols and covered by end-to-end hardware tests.

## Native presentation contract

- Standard titled, resizable macOS window with a unified toolbar.
- Collapsible sidebar using the system sidebar material.
- Semantic colours and SF Symbols.
- A window-owned viewport separates form sizing from window sizing. The sidebar occupies 200 points. Each independently scrollable page uses 24-point margins and the available width, up to 1,320 points on an ultrawide display.
- Workspaces change columns based on their own available width, including when the sidebar is hidden. App launchers and metric grids keep the same controls and reading order during resizing.
- Flat grouped settings rows with native controls; rounded panels are reserved for the physical-device preview or a genuine selection surface.
- Native sheets and `NSAlert` confirmation for modal and destructive workflows.
- Full keyboard-focus, VoiceOver labels, reduced-motion-aware page transitions, and system light/dark appearance.

This keeps the protocol and release surface stable while allowing individual pages to move to SwiftUI later without rewriting the device stack.

The [native layout review](tests/native-layout/README.md) runs the real AppKit interface, records its settled window and content bounds, and captures the pages and sheets for visual inspection.
