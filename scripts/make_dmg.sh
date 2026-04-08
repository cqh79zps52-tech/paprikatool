#!/usr/bin/env bash
#
# Bundle the paprika binary into Paprika Tool.app and write a DMG.
#
# Usage:
#   scripts/make_dmg.sh <path-to-paprika-binary> <output-dir> [version]
#
# Both the bare binary and the .app bundle hold the same Mach-O — the
# binary already contains arm64 + x86_64 slices when built via the project
# default (CMAKE_OSX_ARCHITECTURES="arm64;x86_64").

set -euo pipefail

if [[ "${1:-}" == "" || "${2:-}" == "" ]]; then
    echo "usage: $0 <paprika-binary> <output-dir> [version]" >&2
    exit 2
fi

BIN="$1"
OUT="$2"
VERSION="${3:-1.1.2}"
NAME="Paprika Tool"
IDENT="com.cqh79zps52-tech.paprikatool"

if [[ ! -x "$BIN" ]]; then
    echo "error: $BIN is not an executable file" >&2
    exit 1
fi

mkdir -p "$OUT"
STAGING="$OUT/dmg_staging"
APP="$STAGING/${NAME}.app"
rm -rf "$STAGING"
mkdir -p "$APP/Contents/MacOS"
mkdir -p "$APP/Contents/Resources"

# Copy the binary into the bundle. CFBundleExecutable matches the file name.
cp "$BIN" "$APP/Contents/MacOS/${NAME}"
chmod +x "$APP/Contents/MacOS/${NAME}"

# Verify it's actually a fat binary; warn if not (still works, just not universal).
if command -v lipo >/dev/null 2>&1; then
    echo "[make_dmg] $(lipo -info "$APP/Contents/MacOS/${NAME}")"
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>      <string>en</string>
    <key>CFBundleExecutable</key>             <string>${NAME}</string>
    <key>CFBundleIdentifier</key>             <string>${IDENT}</string>
    <key>CFBundleInfoDictionaryVersion</key>  <string>6.0</string>
    <key>CFBundleName</key>                   <string>${NAME}</string>
    <key>CFBundleDisplayName</key>            <string>${NAME}</string>
    <key>CFBundlePackageType</key>            <string>APPL</string>
    <key>CFBundleSignature</key>              <string>????</string>
    <key>CFBundleShortVersionString</key>     <string>${VERSION}</string>
    <key>CFBundleVersion</key>                <string>${VERSION}</string>
    <key>LSMinimumSystemVersion</key>         <string>10.13</string>
    <key>LSApplicationCategoryType</key>      <string>public.app-category.video</string>
    <key>NSHighResolutionCapable</key>        <true/>
    <key>NSPrincipalClass</key>               <string>NSApplication</string>
    <key>NSSupportsAutomaticGraphicsSwitching</key> <true/>
</dict>
</plist>
PLIST

# Drag-to-install layout: side-by-side .app and an Applications symlink.
ln -s /Applications "$STAGING/Applications"

DMG="$OUT/Paprika-Tool-${VERSION}.dmg"
rm -f "$DMG"

# UDZO = compressed (read-only). Volume name shows up as the mounted disk.
hdiutil create \
    -volname "${NAME}" \
    -srcfolder "$STAGING" \
    -ov \
    -format UDZO \
    -fs HFS+ \
    "$DMG" >/dev/null

# Clean up staging so re-runs are clean.
rm -rf "$STAGING"

ls -lh "$DMG"
echo "[make_dmg] wrote $DMG"
