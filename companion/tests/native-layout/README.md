# Inspecting the native Mac interface

On a Mac with Xcode and a logged-in window session, run:

```sh
python3 companion/tests/native-layout/capture.py --output /tmp/echodesk-captures
python3 companion/tests/native-layout/verify.py /tmp/echodesk-captures
```

The same command runs on the **EchoDesk macOS visual review** Actions runner.
It compiles the shipping AppKit window and page sources into a separate review
bundle, routes its startup to the capture delegate, and opens real native
windows and sheets. It never starts the transport monitor, connects to a port,
checks the network for firmware, or accepts a destructive confirmation.
Each size and appearance gets a separate temporary preferences home. Connected
labels, device identity and alarm data are fixtures; these captures are evidence
about presentation, not tests of device communication.

Inspect the PNGs under **native-layout / window size / light or dark** at full
size. Each folder also includes:

- `geometry.json`: requested and settled window sizes, scroll viewport, usable page width, native bitmap resolution and label/button frames.
- `live-resize.json` in the 1440-point folders: the same Clock settings window resized through all four breakpoints with the sidebar visible and hidden.
- `app.log`: AppKit diagnostics, including any conflicting constraints.

The snapshots use AppKit's own view bitmap cache. They include the title bar and
controls but not the desktop behind the window, the window shadow, or pointer
hover. Native sheets are captured separately. To prepare contact sheets locally
with Pillow, run `python3 companion/tests/native-layout/review.py CAPTURES OUTPUT`;
add `--before BASELINE_CAPTURES` to place baseline and candidate side by side.
