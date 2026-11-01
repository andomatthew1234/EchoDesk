#!/usr/bin/env bash
# Build and merge the production EchoDesk Mini firmware on Linux/macOS CI.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENVIRONMENT="freenove-fnk0114a-2p4"
BUILD_DIR="$PROJECT_DIR/.pio/build/$ENVIRONMENT"
OUTPUT_DIR="${1:-$PROJECT_DIR/dist}"
VERSION="$(sed -n 's/^#define APP_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' "$PROJECT_DIR/src/config.h" | head -1)"

if [[ ! "$VERSION" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$ ]]; then
  echo "ERROR: src/config.h contains an invalid APP_VERSION: '$VERSION'" >&2
  exit 1
fi

PIO_HOME="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
BOOT_APP0="$PIO_HOME/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
OUTPUT_NAME="echodesk-os-freenove-fnk0114a-2p4-v$VERSION.bin"
OUTPUT_BIN="$OUTPUT_DIR/$OUTPUT_NAME"

cd "$PROJECT_DIR"
python3 -m platformio run -e "$ENVIRONMENT"

for file in \
  "$BUILD_DIR/bootloader.bin" \
  "$BUILD_DIR/partitions.bin" \
  "$BUILD_DIR/firmware.bin" \
  "$BOOT_APP0"; do
  if [[ ! -f "$file" ]]; then
    echo "ERROR: required build file is missing: $file" >&2
    exit 1
  fi
done

mkdir -p "$OUTPUT_DIR"
python3 -m esptool --chip esp32 merge_bin \
  -o "$OUTPUT_BIN" \
  --flash_mode dio \
  --flash_freq 40m \
  --flash_size 4MB \
  0x1000 "$BUILD_DIR/bootloader.bin" \
  0x8000 "$BUILD_DIR/partitions.bin" \
  0xe000 "$BOOT_APP0" \
  0x10000 "$BUILD_DIR/firmware.bin"

python3 - "$OUTPUT_BIN" <<'PY'
import hashlib
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
if not 0x11000 <= len(data) <= 0x400000:
    raise SystemExit("merged image is outside the supported 4 MB layout")
if data[0x1000] != 0xE9 or int.from_bytes(data[0x100C:0x100E], "little") != 0:
    raise SystemExit("merged image does not contain an original-ESP32 bootloader at 0x1000")
if data[0x8000:0x8002] != b"\xaa\x50":
    raise SystemExit("merged image does not contain a partition table at 0x8000")
if data[0x10000] != 0xE9:
    raise SystemExit("merged image does not contain an application at 0x10000")
digest = hashlib.sha256(data).hexdigest().upper()
path.with_suffix(path.suffix + ".sha256").write_text(
    f"{digest}  {path.name}\n", encoding="ascii", newline="\n"
)
print(f"EchoDesk Mini {path.name}: {len(data)} bytes, SHA-256 {digest}")
PY
