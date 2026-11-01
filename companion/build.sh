#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

PRODUCT_MODE="${PRODUCT_MODE:-ai-monitor}"
# Convenience/CI compatibility: either invocation builds the branded app.
if [ "${ECHODESK_DESKTOP:-0}" = "1" ]; then PRODUCT_MODE="echodesk"; fi
if [ "$PRODUCT_MODE" = "echodesk" ]; then
  BUILD_DIR="/tmp/echodesk-desktop-build"
  APP_NAME="EchoDesk Desktop"
  APP_VERSION="0.2.8"
  EXECUTABLE_NAME="EchoDeskDesktop"
  INFO_PLIST="Resources/EchoDesk-Info.plist"
  RELEASE_STEM="EchoDesk-Desktop-v${APP_VERSION}-macos-universal"
  SWIFT_DEFINES=(-D ECHODESK_DESKTOP)
else
  BUILD_DIR="/tmp/aimonitor-build"
  APP_NAME="AI Monitor"
  APP_VERSION="1.32.0"
  EXECUTABLE_NAME="AIMonitor"
  INFO_PLIST="Resources/Info.plist"
  RELEASE_STEM="AIMonitor"
  SWIFT_DEFINES=()
fi
APP="$BUILD_DIR/$APP_NAME.app"
ECHODESK_ENTITLEMENTS="$SCRIPT_DIR/Resources/EchoDesk.entitlements"
if [ "$PRODUCT_MODE" = "echodesk" ]; then
  MAIN_ENTITLEMENT_ARGS=(--entitlements "$ECHODESK_ENTITLEMENTS")
else
  MAIN_ENTITLEMENT_ARGS=()
fi
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Developer ID Signing (ab v1.13.0) — optional. Wenn die Identity nicht im
# Keychain ist (z.B. CI-Runner ohne Cert-Import), fallen wir auf Ad-hoc-Sign
# zurück, damit der lokale Build weiter funktioniert. Notarization/Stapling
# werden nur dann ausgeführt, wenn SIGN_IDENTITY gesetzt ist UND die Env-Var
# NOTARIZE=1 (oder das Keychain-Profil $NOTARY_PROFILE verfügbar) aktiv ist.
SIGN_IDENTITY_DEFAULT="Developer ID Application: Tobias Marks (7V4K87652E)"
SIGN_IDENTITY="${SIGN_IDENTITY:-$SIGN_IDENTITY_DEFAULT}"
NOTARY_PROFILE="${NOTARY_PROFILE:-AC_NOTARY}"

# Prüfe, ob die Developer-ID-Identity im Keychain ist.
if security find-identity -v -p codesigning 2>/dev/null | grep -q "$SIGN_IDENTITY"; then
  HAS_DEVELOPER_ID=1
  echo "Developer ID available: $SIGN_IDENTITY"
else
  HAS_DEVELOPER_ID=0
  echo "Developer ID NOT found — falling back to ad-hoc signing (no notarization)."
fi

echo "Compiling $APP_NAME v${APP_VERSION}..."

# Baue .app Bundle Struktur zuerst
mkdir -p "$APP/Contents/MacOS"
mkdir -p "$APP/Contents/Resources"

# Universal Binary (ab v1.27.0): arm64 + x86_64.
#
# Intel wird bewusst weiter bedient — CodexBar, die Pflicht-Datenquelle, ist
# selbst universal und liefert eine x86_64-CLI. Ein Ausschluss waere allein
# ein Nebeneffekt des Build-Targets, kein technischer Zwang.
#
# MACOS_MIN richtet sich nach CodexBar (macOS 14+); tiefer brachte nichts,
# weil ohne CodexBar keine Daten kommen. Der Swift-Code selbst kompiliert
# auch gegen 13.0 — die frueheren 15.0 waren nicht durch APIs begruendet.
MACOS_MIN="14.0"
SWIFT_SOURCES=(
  Sources/main.swift
  Sources/CodexBarSource.swift
  Sources/StatusIndicator.swift
  Sources/NotificationBanner.swift
  Sources/Typography.swift
  Sources/Localization.swift
  Sources/SettingsWindow.swift
  Sources/SettingsWindow+Overview.swift
  Sources/SettingsWindow+Display.swift
  Sources/SettingsWindow+Views.swift
  Sources/SettingsWindow+Connection.swift
  Sources/SettingsWindow+Updates.swift
  Sources/SettingsWindow+Diagnostics.swift
  Sources/DisplayPlugins.swift
  Sources/IntelligentViews.swift
  Sources/SettingsWindow+Plugins.swift
  Sources/ClaudeCodeWindow.swift
  Sources/EchoDeskWifiSetup.swift
  Sources/EchoDeskFirmwareBundle.swift
  Sources/EchoDeskFirmwareUpdates.swift
  Sources/EchoDeskTelemetry.swift
  Sources/SettingsWindow+EchoDeskWifiSetup.swift
  Sources/SettingsWindow+EchoDeskDesktop.swift
)

SLICES=()
for ARCH in arm64 x86_64; do
  echo "Compiling slice: $ARCH (macOS $MACOS_MIN)"
  swiftc \
    "${SWIFT_SOURCES[@]}" \
    "${SWIFT_DEFINES[@]}" \
    -framework Cocoa \
    -framework Security \
    -framework Network \
    -framework ServiceManagement \
    -framework IOKit \
    -framework CoreAudio \
    -target "$ARCH-apple-macosx$MACOS_MIN" \
    -O \
    -o "$BUILD_DIR/$EXECUTABLE_NAME-$ARCH"
  SLICES+=("$BUILD_DIR/$EXECUTABLE_NAME-$ARCH")
done

lipo -create -output "$APP/Contents/MacOS/$EXECUTABLE_NAME" "${SLICES[@]}"
rm -f "${SLICES[@]}"

# EchoDesk Desktop is self-contained: pin and bundle the upstream CodexBar CLI
# instead of depending on a separate CodexBar/Homebrew installation.
if [ "$PRODUCT_MODE" = "echodesk" ]; then
CODEXBAR_VERSION="${CODEXBAR_VERSION:-0.82.0}"
CODEXBAR_SHA256_arm64="5e6d922fbebbd41a748bc906500740529265e65afbc7ffd2ce8c8794fc5edf76"
CODEXBAR_SHA256_x86_64="aca88b9b916197e5e0dba33c0e2a8e78f27d4a40760acf849509060b630cea0c"
CODEXBAR_CACHE="${CODEXBAR_CACHE:-$HOME/.cache/echodesk-codexbar}"
CODEXBAR_SLICES=()
mkdir -p "$APP/Contents/Helpers" "$CODEXBAR_CACHE"
for ARCH in arm64 x86_64; do
  ARCHIVE="CodexBarCLI-v${CODEXBAR_VERSION}-macos-${ARCH}.tar.gz"
  TARBALL="$CODEXBAR_CACHE/$ARCHIVE"
  EXPECTED_VAR="CODEXBAR_SHA256_${ARCH}"
  EXPECTED_SHA="${!EXPECTED_VAR}"
  if [ ! -f "$TARBALL" ]; then
    URL="https://github.com/steipete/CodexBar/releases/download/v${CODEXBAR_VERSION}/${ARCHIVE}"
    curl -fL --retry 3 --retry-delay 2 -o "$TARBALL.part" "$URL"
    mv "$TARBALL.part" "$TARBALL"
  fi
  ACTUAL_SHA=$(shasum -a 256 "$TARBALL" | awk '{print tolower($1)}')
  [ "$ACTUAL_SHA" = "$EXPECTED_SHA" ] \
    || { echo "ERROR: CodexBar CLI $ARCH archive SHA-256 mismatch"; exit 1; }
  TMP_CODEXBAR=$(mktemp -d)
  tar -xzf "$TARBALL" -C "$TMP_CODEXBAR"
  CODEXBAR_BIN=$(find "$TMP_CODEXBAR" -type f -name CodexBarCLI -perm +111 | head -1)
  [ -n "$CODEXBAR_BIN" ] || { echo "ERROR: $ARCH CodexBarCLI is missing"; exit 1; }
  cp "$CODEXBAR_BIN" "$BUILD_DIR/CodexBarCLI-$ARCH"
  rm -rf "$TMP_CODEXBAR"
  CODEXBAR_SLICES+=("$BUILD_DIR/CodexBarCLI-$ARCH")
done
lipo -create -output "$APP/Contents/Helpers/CodexBarCLI" "${CODEXBAR_SLICES[@]}"
chmod +x "$APP/Contents/Helpers/CodexBarCLI"
rm -f "${CODEXBAR_SLICES[@]}"
CODEXBAR_ARCHS=$(lipo -archs "$APP/Contents/Helpers/CodexBarCLI")
for NEED in arm64 x86_64; do
  case " $CODEXBAR_ARCHS " in *" $NEED "*) ;; *) echo "ERROR: bundled CodexBarCLI lacks $NEED"; exit 1 ;; esac
done
CODEXBAR_REPORTED_VERSION=$("$APP/Contents/Helpers/CodexBarCLI" --version 2>&1 | head -1)
case "$CODEXBAR_REPORTED_VERSION" in
  *"$CODEXBAR_VERSION"*) ;;
  # CodexBar CLI 0.82.0 reports only its product name. The two release archive
  # hashes above are the authoritative version pins in that case.
  "CodexBar") ;;
  *) echo "ERROR: unexpected CodexBarCLI version: $CODEXBAR_REPORTED_VERSION"; exit 1 ;;
esac
"$APP/Contents/Helpers/CodexBarCLI" usage --help 2>&1 | grep -q -- '--source' \
  || { echo "ERROR: bundled CodexBarCLI lacks OAuth source selection"; exit 1; }
echo "Validated bundled command contract: codexbar usage --provider codex --json --source oauth"
fi

# The native companion uses the same validated plugin format and scene builder
# as the Windows app through a small bundled Rust helper.
command -v cargo >/dev/null || { echo "ERROR: Rust/Cargo fehlt für Display-Plugins"; exit 1; }
command -v rustup >/dev/null || { echo "ERROR: rustup fehlt für den Universal-Build des Plugin-Helfers"; exit 1; }
rustup target add aarch64-apple-darwin x86_64-apple-darwin >/dev/null
PLUGIN_MANIFEST="$SCRIPT_DIR/../companion-windows/Cargo.toml"
for ARCH in aarch64-apple-darwin x86_64-apple-darwin; do
  cargo build --manifest-path "$PLUGIN_MANIFEST" -p aimonitor-plugin-host --release --target "$ARCH" --locked
done
lipo -create -output "$APP/Contents/MacOS/aimonitor-plugin-host" \
  "$SCRIPT_DIR/../companion-windows/target/aarch64-apple-darwin/release/aimonitor-plugin-host" \
  "$SCRIPT_DIR/../companion-windows/target/x86_64-apple-darwin/release/aimonitor-plugin-host"

# Gegenprobe: beide Slices muessen drin sein, sonst waere der Universal-Build
# still zu einem Single-Arch-Build degradiert.
ARCHS_BUILT=$(lipo -archs "$APP/Contents/MacOS/$EXECUTABLE_NAME")
echo "Universal binary: $ARCHS_BUILT"
for NEED in arm64 x86_64; do
  case " $ARCHS_BUILT " in
    *" $NEED "*) ;;
    *) echo "ERROR: slice $NEED fehlt im finalen Binary"; exit 1 ;;
  esac
done

# Info.plist + Resources
cp "$INFO_PLIST" "$APP/Contents/Info.plist"

if [ "$PRODUCT_MODE" = "echodesk" ]; then
  FIRMWARE_SOURCE="$SCRIPT_DIR/../desktop/firmware/echodesk-mini/2.32.0/echodesk-os.bin"
  FIRMWARE_DEST="$APP/Contents/Resources/firmware/echodesk-mini/2.32.0"
  [ -f "$FIRMWARE_SOURCE" ] || { echo "ERROR: EchoDesk OS 2.32.0 is missing"; exit 1; }
  EXPECTED_FIRMWARE_SHA="1109b7a4afb242b9695ae60952073fd6da2e2e33e9fe53c1d000885af67dd2ea"
  ACTUAL_FIRMWARE_SHA=$(shasum -a 256 "$FIRMWARE_SOURCE" | awk '{print tolower($1)}')
  [ "$ACTUAL_FIRMWARE_SHA" = "$EXPECTED_FIRMWARE_SHA" ] \
    || { echo "ERROR: EchoDesk OS 2.32.0 SHA-256 mismatch"; exit 1; }
  [ "$(stat -f %z "$FIRMWARE_SOURCE")" = "1552080" ] \
    || { echo "ERROR: EchoDesk OS 2.32.0 size mismatch"; exit 1; }
  mkdir -p "$FIRMWARE_DEST"
  cp "$FIRMWARE_SOURCE" "$FIRMWARE_DEST/echodesk-os.bin"
  echo "Bundled and validated EchoDesk OS 2.32.0"
fi

# -----------------------------------------------------------------------------
# App-Icon (ab v1.23.0)
# -----------------------------------------------------------------------------
# Resources/AppIcon.icon ist ein Icon-Composer-Dokument mit getrennten Ebenen.
# actool rendert daraus Assets.car (das geschichtete Icon, auf das macOS 26 die
# Liquid-Glass-Effekte anwendet) und ein AppIcon.icns als Fallback fuer aeltere
# Systeme. Die Quell-Ebenen liegen unter Resources/IconLayers/.
#
# Wichtig: das frueher hier kopierte, statische Resources/AppIcon.icns wird
# NICHT mehr ins Bundle gelegt — es bringt eine eingebackene Maske mit, die mit
# der Systemform von macOS 26 kollidiert.
# Ausserhalb von BUILD_DIR ablegen — der ganze BUILD_DIR wird spaeter nach
# build/ kopiert, die Zwischendatei hat dort nichts verloren.
ICON_PARTIAL_PLIST="$(mktemp -t aimonitor-icon).plist"
xcrun actool "$SCRIPT_DIR/Resources/AppIcon.icon" \
  --compile "$APP/Contents/Resources" \
  --platform macosx \
  --minimum-deployment-target 15.0 \
  --app-icon AppIcon \
  --include-all-app-icons \
  --output-partial-info-plist "$ICON_PARTIAL_PLIST" \
  --errors --warnings > /dev/null

if [ ! -f "$APP/Contents/Resources/Assets.car" ]; then
  echo "ERROR: actool hat kein Assets.car erzeugt — App-Icon fehlt."
  exit 1
fi

# Die von actool gemeldeten Schluessel in die Info.plist uebernehmen.
while IFS= read -r key; do
  value=$(/usr/libexec/PlistBuddy -c "Print :$key" "$ICON_PARTIAL_PLIST")
  /usr/libexec/PlistBuddy -c "Delete :$key" "$APP/Contents/Info.plist" 2>/dev/null || true
  /usr/libexec/PlistBuddy -c "Add :$key string $value" "$APP/Contents/Info.plist"
done < <(/usr/libexec/PlistBuddy -c "Print" "$ICON_PARTIAL_PLIST" \
          | grep -oE '^ *[A-Za-z]+ = ' | tr -d ' =')
echo "App-Icon aus AppIcon.icon kompiliert (Assets.car + AppIcon.icns)"
# Menubar-Icons werden ab v1.8.0 nicht mehr verwendet (LSUIElement unsichtbar),
# bleiben aber im Repo für den Fall, dass wir die Entscheidung revidieren.
# Lokalisierung: de.lproj / en.lproj ins Bundle (App folgt der Systemsprache).
for lang in de en; do
  if [ -d "Resources/$lang.lproj" ]; then
    mkdir -p "$APP/Contents/Resources/$lang.lproj"
    cp "Resources/$lang.lproj/Localizable.strings" "$APP/Contents/Resources/$lang.lproj/"
  fi
done

cp Resources/MenuBarIconTemplate.png "$APP/Contents/Resources/" 2>/dev/null || true
cp Resources/MenuBarIconTemplate@2x.png "$APP/Contents/Resources/" 2>/dev/null || true
if [ "$PRODUCT_MODE" = "echodesk" ]; then
  cp THIRD-PARTY-NOTICES.md "$APP/Contents/Resources/"
fi

# Bundle esptool als eigenstaendige Binaries (ab v1.27.1).
#
# Vorher lag hier das Python-Paket aus PlatformIO plus per pip gevendorte
# Abhaengigkeiten. Das hatte zwei Fehler, die erst mit dem Universal Build
# sichtbar wurden:
#   1. pip zieht Wheels fuer die Architektur des BUILD-Rechners. Auf einem
#      Apple-Silicon-Runner landeten arm64-only .so-Dateien im Bundle —
#      auf Intel waere das Flashen gescheitert.
#   2. Die Extensions sind an eine Python-Minor-Version gebunden
#      (cpython-314). Mit einem anderen python3 im PATH brach der Import.
#
# Espressif liefert esptool als eigenstaendige Binaries je Architektur.
# Damit entfaellt die Python-Abhaengigkeit fuer das Flashen komplett.
ESPTOOL_VERSION="${ESPTOOL_VERSION:-5.4.0}"
ESPTOOL_CACHE="${ESPTOOL_CACHE:-$HOME/.cache/aimonitor-esptool}"
ESPTOOL_DEST="$APP/Contents/Resources/esptool-bin"
mkdir -p "$ESPTOOL_DEST" "$ESPTOOL_CACHE"

# Espressif nennt die Intel-Variante "amd64"; im Bundle heisst sie wie die
# Swift-Architektur "x86_64", damit die App sie direkt adressieren kann.
for PAIR in "arm64:arm64" "amd64:x86_64"; do
  UP_ARCH="${PAIR%%:*}"
  OUT_ARCH="${PAIR##*:}"
  TARBALL="$ESPTOOL_CACHE/esptool-v$ESPTOOL_VERSION-macos-$UP_ARCH.tar.gz"
  if [ ! -f "$TARBALL" ]; then
    URL="https://github.com/espressif/esptool/releases/download/v$ESPTOOL_VERSION/esptool-v$ESPTOOL_VERSION-macos-$UP_ARCH.tar.gz"
    echo "Downloading esptool $ESPTOOL_VERSION ($UP_ARCH)..."
    # --retry/--continue-at: der Download ist ~60 MB und darf einen Abbruch
    # ueberleben. Erst nach vollstaendigem Transfer umbenennen, damit eine
    # abgebrochene .part nie als gueltiges Archiv missverstanden wird.
    curl -fL --retry 3 --retry-delay 2 --continue-at - \
      -o "$TARBALL.part" "$URL" \
      || { echo "ERROR: esptool download failed ($UP_ARCH)"; rm -f "$TARBALL.part"; exit 1; }
    tar -tzf "$TARBALL.part" >/dev/null 2>&1 \
      || { echo "ERROR: esptool archive corrupt ($UP_ARCH)"; rm -f "$TARBALL.part"; exit 1; }
    mv "$TARBALL.part" "$TARBALL"
  fi
  TMP_X=$(mktemp -d)
  tar -xzf "$TARBALL" -C "$TMP_X"
  # Nur das esptool-Binary uebernehmen — espefuse und esp_rfc2217_server
  # werden nicht gebraucht und wuerden das Bundle unnoetig aufblaehen.
  SRC_BIN=$(find "$TMP_X" -type f -name esptool -perm +111 | head -1)
  [ -n "$SRC_BIN" ] || { echo "ERROR: esptool binary not found in $UP_ARCH tarball"; exit 1; }
  cp "$SRC_BIN" "$ESPTOOL_DEST/esptool-$OUT_ARCH"
  chmod +x "$ESPTOOL_DEST/esptool-$OUT_ARCH"
  rm -rf "$TMP_X"

  # Gegenprobe: Slice muss zur erwarteten Architektur passen, sonst waere
  # genau der Fehler zurueck, den dieser Umbau beseitigt.
  GOT=$(lipo -archs "$ESPTOOL_DEST/esptool-$OUT_ARCH" 2>/dev/null)
  case " $GOT " in
    *" $OUT_ARCH "*) echo "Bundled esptool $ESPTOOL_VERSION ($OUT_ARCH)" ;;
    *) echo "ERROR: esptool-$OUT_ARCH hat Architektur '$GOT'"; exit 1 ;;
  esac
done

# =============================================================================
# Code-Signing
# =============================================================================
if [ "$HAS_DEVELOPER_ID" = "1" ]; then
  echo "Signing with Developer ID (Hardened Runtime + Timestamp)..."

  # Inside-Out-Signing: Zuerst die eingebetteten Mach-O-Binaries signieren,
  # dann erst die .app selbst. --deep ist fuer Distribution deprecated, wir
  # machen es manuell. Die esptool-Binaries laufen als eigener Prozess, aber
  # fuer die Notarisierung muessen ALLE Mach-O-Dateien im Bundle mit unserer
  # Developer ID + Hardened Runtime + Timestamp signiert sein — die Signatur
  # von Espressif wird dabei ersetzt.
  # Die esptool-Binaries werden BEWUSST NICHT neu signiert.
  #
  # Es sind PyInstaller-Bundles: sie entpacken zur Laufzeit ein eigenes
  # Python-Framework und laden es per dlopen. Signiert man nur die aeussere
  # Huelle mit unserer Developer ID, traegt die eingebettete Library weiter
  # Espressifs Team-ID — der Hardened Runtime bricht den Ladevorgang dann ab
  # ("mapping process and mapped file have different Team IDs") und das
  # Flashen scheitert. Espressif signiert die Binaries bereits selbst mit
  # Developer ID und Hardened Runtime; eingebettete Helfer duerfen fremd
  # signiert sein, solange die Signatur gueltig ist.
  if [ -d "$APP/Contents/Resources/esptool-bin" ]; then
    for TOOL in "$APP/Contents/Resources/esptool-bin/"esptool-*; do
      [ -f "$TOOL" ] || continue
      codesign --verify --strict "$TOOL" 2>/dev/null \
        || { echo "ERROR: $(basename "$TOOL") hat keine gueltige Signatur"; exit 1; }
    done
    echo "Verified vendor signatures of bundled esptool binaries"
  fi

  # Main app binary + Bundle. Mit --options runtime = Hardened Runtime.
  # EchoDesk sends Apple Events to Music/Spotify for Now Playing, so its main
  # executable needs the narrowly scoped Automation entitlement in addition
  # to the NSAppleEventsUsageDescription permission prompt.
  codesign --force --timestamp --options runtime \
    --sign "$SIGN_IDENTITY" "$APP/Contents/MacOS/aimonitor-plugin-host"
  if [ -f "$APP/Contents/Helpers/CodexBarCLI" ]; then
    codesign --force --timestamp --options runtime \
      --sign "$SIGN_IDENTITY" "$APP/Contents/Helpers/CodexBarCLI"
  fi
  codesign --force --timestamp --options runtime \
    "${MAIN_ENTITLEMENT_ARGS[@]}" \
    --sign "$SIGN_IDENTITY" "$APP/Contents/MacOS/$EXECUTABLE_NAME"
  codesign --force --timestamp --options runtime \
    "${MAIN_ENTITLEMENT_ARGS[@]}" \
    --sign "$SIGN_IDENTITY" "$APP"

  # Sanity-Check
  codesign --verify --deep --strict --verbose=2 "$APP"
  echo "Developer ID signing complete."
else
  # Ad-hoc-Fallback (CI ohne Cert, lokal ohne Apple-Dev-Account)
  codesign --force --deep --sign - \
    "${MAIN_ENTITLEMENT_ARGS[@]}" "$APP"
fi

# Kopiere zurück ins Projekt
rm -rf "$SCRIPT_DIR/build"
cp -R "$BUILD_DIR" "$SCRIPT_DIR/build"

# =============================================================================
# DMG-Build + Signing
# =============================================================================
# DMG aus dem (evtl. gestapelten) App-Bundle erzeugen. Reihenfolge:
#   1. DMG bauen
#   2. DMG signieren (Developer ID)
#   3. DMG bei Apple zur Notarization einreichen
#   4. Ticket an DMG stapeln
#   5. App einzeln stapeln (damit die ZIP-Version das Ticket enthält)
#   6. ZIP aus gestapelter .app erzeugen (ditto)
DMG_PATH="$SCRIPT_DIR/build/$RELEASE_STEM.dmg"
rm -f "$DMG_PATH"

if [ "$PRODUCT_MODE" = "echodesk" ]; then
  # The supplied artwork is a 2x 1536x1024 canvas. Finder's icon-view window
  # uses the matching 768x512 point layout; sips creates the deterministic 1x
  # backing file that Finder records in .DS_Store.
  DMG_BACKGROUND_SOURCE="$SCRIPT_DIR/Resources/DMG/EchoDesk-DMG-Background.png"
  [ -f "$DMG_BACKGROUND_SOURCE" ] || {
    echo "ERROR: EchoDesk DMG background is missing: $DMG_BACKGROUND_SOURCE"
    exit 1
  }
  [ -x /usr/bin/sips ] || { echo "ERROR: /usr/bin/sips is required to build the EchoDesk DMG"; exit 1; }
  [ -x /usr/bin/osascript ] || { echo "ERROR: /usr/bin/osascript is required to style the EchoDesk DMG"; exit 1; }
  [ -x /usr/bin/hdiutil ] || { echo "ERROR: /usr/bin/hdiutil is required to build the EchoDesk DMG"; exit 1; }

  BG_WIDTH=$(/usr/bin/sips -g pixelWidth "$DMG_BACKGROUND_SOURCE" 2>/dev/null | awk '/pixelWidth:/ {print $2}')
  BG_HEIGHT=$(/usr/bin/sips -g pixelHeight "$DMG_BACKGROUND_SOURCE" 2>/dev/null | awk '/pixelHeight:/ {print $2}')
  [ "$BG_WIDTH" = "1536" ] && [ "$BG_HEIGHT" = "1024" ] || {
    echo "ERROR: EchoDesk DMG background must be 1536x1024 (found ${BG_WIDTH:-?}x${BG_HEIGHT:-?})"
    exit 1
  }

  DMG_WORK_DIR="$(mktemp -d /tmp/echodesk-dmg.XXXXXX)"
  DMG_MOUNT_DIR=""
  DMG_DEVICE=""
  DMG_RW_PATH="$DMG_WORK_DIR/EchoDesk-Desktop-writable.dmg"
  DMG_IS_MOUNTED=0

  cleanup_echodesk_dmg() {
    if [ "$DMG_IS_MOUNTED" = "1" ]; then
      DMG_DETACH_TARGET="${DMG_MOUNT_DIR:-$DMG_DEVICE}"
      /usr/bin/hdiutil detach "$DMG_DETACH_TARGET" >/dev/null 2>&1 \
        || /usr/bin/hdiutil detach "$DMG_DETACH_TARGET" -force >/dev/null 2>&1 \
        || true
    fi
    case "$DMG_WORK_DIR" in
      /tmp/echodesk-dmg.*) rm -rf -- "$DMG_WORK_DIR" ;;
    esac
  }
  trap cleanup_echodesk_dmg EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM

  APP_KB=$(du -sk "$SCRIPT_DIR/build/$APP_NAME.app" | awk '{print $1}')
  DMG_SIZE_KB=$((APP_KB + 65536))
  # A blank image is UDRW by default. Newer hdiutil versions reject an
  # explicit -format unless create is also given -srcfolder/-srcdevice.
  /usr/bin/hdiutil create -ov -size "${DMG_SIZE_KB}k" -fs HFS+ \
    -volname "$APP_NAME" "$DMG_RW_PATH" >/dev/null || {
      echo "ERROR: Could not create the writable EchoDesk DMG"
      exit 1
    }
  if ! DMG_ATTACH_OUTPUT=$(/usr/bin/hdiutil attach "$DMG_RW_PATH" \
    -readwrite -noverify -noautoopen); then
      echo "ERROR: Could not mount the writable EchoDesk DMG"
      exit 1
  fi
  DMG_MOUNT_DIR=$(printf '%s\n' "$DMG_ATTACH_OUTPUT" \
    | awk -F '\t' '$NF ~ /^\/Volumes\// { print $NF; exit }')
  DMG_DEVICE=$(printf '%s\n' "$DMG_ATTACH_OUTPUT" \
    | awk -F '\t' '$NF ~ /^\/Volumes\// { print $1; exit }')
  DMG_IS_MOUNTED=1
  [ -n "$DMG_MOUNT_DIR" ] && [ -d "$DMG_MOUNT_DIR" ] || {
    echo "ERROR: Could not identify the Finder-visible EchoDesk DMG mount"
    exit 1
  }
  DMG_FINDER_DISK_NAME=${DMG_MOUNT_DIR#/Volumes/}

  ditto "$SCRIPT_DIR/build/$APP_NAME.app" "$DMG_MOUNT_DIR/$APP_NAME.app"
  ln -s /Applications "$DMG_MOUNT_DIR/Applications"
  mkdir -p "$DMG_MOUNT_DIR/.background"
  /usr/bin/sips -z 512 768 "$DMG_BACKGROUND_SOURCE" \
    --out "$DMG_MOUNT_DIR/.background/EchoDesk-DMG-Background.png" >/dev/null
  SCALED_WIDTH=$(/usr/bin/sips -g pixelWidth "$DMG_MOUNT_DIR/.background/EchoDesk-DMG-Background.png" 2>/dev/null | awk '/pixelWidth:/ {print $2}')
  SCALED_HEIGHT=$(/usr/bin/sips -g pixelHeight "$DMG_MOUNT_DIR/.background/EchoDesk-DMG-Background.png" 2>/dev/null | awk '/pixelHeight:/ {print $2}')
  [ "$SCALED_WIDTH" = "768" ] && [ "$SCALED_HEIGHT" = "512" ] || {
    echo "ERROR: Could not scale the EchoDesk DMG background to 768x512"
    exit 1
  }

  # The background contains the arrow and installation instruction. Positions
  # below are the centres of its two rounded icon panels.
  configure_echodesk_dmg_finder() {
    /usr/bin/osascript <<OSA
with timeout of 30 seconds
  tell application "Finder"
    tell disk "$DMG_FINDER_DISK_NAME"
      try
        close container window
      end try
      open
      delay 2
      set current view of container window to icon view
      set toolbar visible of container window to false
      set statusbar visible of container window to false
      set pathbar visible of container window to false
      set sidebar width of container window to 0
      set bounds of container window to {100, 100, 868, 612}
      set viewOptions to icon view options of container window
      set arrangement of viewOptions to not arranged
      set icon size of viewOptions to 128
      set text size of viewOptions to 14
      set label position of viewOptions to bottom
      set shows item info of viewOptions to false
      set shows icon preview of viewOptions to true
      set background picture of viewOptions to file ".background:EchoDesk-DMG-Background.png"
      set extension hidden of item "$APP_NAME.app" to true
      set position of item "$APP_NAME.app" to {168, 265}
      set position of item "Applications" to {600, 265}
      update without registering applications
      delay 3
      close
    end tell
  end tell
end timeout
OSA
  }

  DMG_FINDER_CONFIGURED=0
  for DMG_FINDER_ATTEMPT in 1 2 3 4; do
    if ! /usr/bin/hdiutil info | grep -Fq -- "$DMG_MOUNT_DIR"; then
      echo "ERROR: EchoDesk DMG mount disappeared before Finder layout attempt $DMG_FINDER_ATTEMPT"
      break
    fi
    if configure_echodesk_dmg_finder; then
      DMG_FINDER_CONFIGURED=1
      break
    fi
    echo "WARNING: Finder layout attempt $DMG_FINDER_ATTEMPT/4 failed; retrying..."
    sleep $((DMG_FINDER_ATTEMPT * 2))
  done
  if [ "$DMG_FINDER_CONFIGURED" != "1" ]; then
    echo "ERROR: Finder could not configure the EchoDesk DMG layout"
    exit 1
  fi
  sync
  [ -f "$DMG_MOUNT_DIR/.DS_Store" ] || {
    echo "ERROR: Finder did not persist the EchoDesk DMG layout (.DS_Store missing)"
    exit 1
  }
  /usr/bin/hdiutil detach "$DMG_MOUNT_DIR" >/dev/null || {
    echo "ERROR: Could not detach the configured EchoDesk DMG"
    exit 1
  }
  DMG_IS_MOUNTED=0
  /usr/bin/hdiutil convert "$DMG_RW_PATH" -format UDZO -imagekey zlib-level=9 \
    -o "$DMG_PATH" >/dev/null || {
      echo "ERROR: Could not convert the configured EchoDesk DMG to UDZO"
      exit 1
    }
  cleanup_echodesk_dmg
  trap - EXIT INT TERM
  [ -s "$DMG_PATH" ] || { echo "ERROR: EchoDesk DMG output is missing or empty"; exit 1; }
  echo "Built styled EchoDesk DMG: $DMG_PATH"
else
  # Preserve the original AI Monitor packaging path.
  DMG_STAGING="$(mktemp -d)"
  cp -R "$SCRIPT_DIR/build/$APP_NAME.app" "$DMG_STAGING/"
  ln -sf /Applications "$DMG_STAGING/Applications"
  if hdiutil create -volname "$APP_NAME" \
    -srcfolder "$DMG_STAGING" \
    -ov -format UDZO \
    "$DMG_PATH" >/dev/null 2>&1; then
    echo "Built DMG: $DMG_PATH"
  else
    echo "WARNING: hdiutil DMG build failed — skipping notarization."
    DMG_PATH=""
  fi
  rm -rf "$DMG_STAGING"
fi

# =============================================================================
# Notarization + Stapling (nur mit Developer ID + NOTARIZE=1)
# =============================================================================
NOTARIZE="${NOTARIZE:-0}"
if [ "$HAS_DEVELOPER_ID" = "1" ] && [ "$NOTARIZE" = "1" ] && [ -n "$DMG_PATH" ]; then
  echo "Signing DMG with Developer ID..."
  codesign --force --timestamp --sign "$SIGN_IDENTITY" "$DMG_PATH"

  echo "Submitting DMG to Apple notarization (keychain-profile=$NOTARY_PROFILE)..."
  echo "  This typically takes 1-5 minutes."
  if xcrun notarytool submit "$DMG_PATH" \
    --keychain-profile "$NOTARY_PROFILE" \
    --wait; then
    echo "Notarization ACCEPTED."

    echo "Stapling ticket to DMG and App..."
    xcrun stapler staple "$DMG_PATH"
    xcrun stapler staple "$SCRIPT_DIR/build/$APP_NAME.app"
    xcrun stapler validate "$SCRIPT_DIR/build/$APP_NAME.app"
    xcrun stapler validate "$DMG_PATH"
    echo "Stapling complete."
  else
    echo "ERROR: Notarization failed. Run:"
    echo "  xcrun notarytool history --keychain-profile $NOTARY_PROFILE"
    echo "  xcrun notarytool log <submission-id> --keychain-profile $NOTARY_PROFILE"
    exit 1
  fi
else
  if [ "$HAS_DEVELOPER_ID" = "1" ]; then
    echo "Skipping notarization (NOTARIZE=1 not set). DMG is signed but not notarized."
  fi
fi

# =============================================================================
# ZIP aus (gestapelter) .app erzeugen. ditto erhält die Signatur und
# Extended Attributes korrekt — besser als zip(1) für signierte Bundles.
# =============================================================================
cd "$SCRIPT_DIR/build"
rm -f "$RELEASE_STEM.zip"
ditto -c -k --keepParent "$APP_NAME.app" "$RELEASE_STEM.zip"
echo "Release ZIP: $SCRIPT_DIR/build/$RELEASE_STEM.zip"

# =============================================================================
# Verifikation (nur bei Developer-ID-Build)
# =============================================================================
if [ "$HAS_DEVELOPER_ID" = "1" ] && [ "$NOTARIZE" = "1" ]; then
  echo ""
  echo "=== Gatekeeper-Verifikation ==="
  spctl -a -vvv -t install "$DMG_PATH" 2>&1 || true
  spctl -a -vvv -t execute "$SCRIPT_DIR/build/$APP_NAME.app" 2>&1 || true
  xcrun stapler validate "$SCRIPT_DIR/build/$APP_NAME.app" 2>&1 || true
  xcrun stapler validate "$DMG_PATH" 2>&1 || true
fi

echo ""
echo "Build complete: $SCRIPT_DIR/build/$APP_NAME.app"
echo ""
echo "=== Release Workflow ==="
echo "1. Update kAppVersion in Sources/main.swift"
echo "2. Update CFBundleVersion + CFBundleShortVersionString in Resources/Info.plist"
echo "3. Update APP_VERSION in build.sh (this file)"
echo "4. Run: ../scripts/sync_site_versions.sh   (Installer-Seite nachziehen)"
echo "5. Commit + push"
echo "6. git tag app-vX.Y.Z && git push origin app-vX.Y.Z"
echo "   -> CI baut signiert + notarisiert + gestapelt, legt das Release an"
echo "      und haengt ZIP + DMG an. Ein lokaler Build ist dafuer nicht noetig."
echo ""
echo "Eigene Release-Notes? Vorher ein Release (auch Draft) mit dem Tag"
echo "anlegen — CI laesst vorhandene Notes unveraendert und ergaenzt nur"
echo "die Assets."
echo ""
echo "Lokaler Build hier: ./build.sh zum Testen, NOTARIZE=1 ./build.sh"
echo "fuer ein verteilbares Bundle ausserhalb des Release-Flows."
