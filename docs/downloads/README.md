# EchoDesk Desktop website downloads

The release page links to these versioned GitHub Release assets:

- `EchoDesk-Desktop-v0.2.8-win-x64-setup.exe`
- `EchoDesk-Desktop-v0.2.8-win-x64.zip`
- `EchoDesk-Desktop-v0.2.8-macos-universal.dmg`
- `EchoDesk-Desktop-v0.2.8-macos-universal.zip`

Large executable binaries are not committed to Git history. Publish the exact
verified files as release `v0.2.8`; do not rename an older build to satisfy the
links. The final size and SHA-256 values in `../download.html` must come from
those same files. The macOS artifacts must be built as universal binaries for
Apple silicon and Intel; do not describe an ad-hoc-signed build as notarized.
