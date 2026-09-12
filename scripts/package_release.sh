#!/bin/bash
# Package an already-built, signed universal bundle. Run from any directory.
set -euo pipefail
JITTER_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
JITTER_BUNDLE="${1:-$JITTER_ROOT/Mac/build/Release/Jitter.plugin}"
JITTER_VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$JITTER_BUNDLE/Contents/Info.plist")
[[ "$JITTER_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid bundle version' >&2; exit 1; }
codesign --verify --strict "$JITTER_BUNDLE"
lipo "$JITTER_BUNDLE/Contents/MacOS/Jitter" -verify_arch arm64 x86_64
JITTER_STAGE=$(mktemp -d /tmp/jitter-package.XXXXXX)
trap 'rm -rf "$JITTER_STAGE"' EXIT
mkdir -p "$JITTER_ROOT/dist" "$JITTER_STAGE/Jitter/assets"
ditto --norsrc --noextattr --noacl "$JITTER_BUNDLE" "$JITTER_STAGE/Jitter/Jitter.plugin"
for file in install.sh README.md INSTALL.md CHANGELOG.md BUILD.md LICENSE; do
  cp "$JITTER_ROOT/$file" "$JITTER_STAGE/Jitter/$file"
done
cp "$JITTER_ROOT/assets/jitter.png" "$JITTER_STAGE/Jitter/assets/jitter.png"
codesign --verify --strict "$JITTER_STAGE/Jitter/Jitter.plugin"
JITTER_ZIP="Jitter-v$JITTER_VERSION-macOS-universal.zip"
ditto -c -k --norsrc --noextattr --keepParent "$JITTER_STAGE/Jitter" "$JITTER_ROOT/dist/$JITTER_ZIP"
(cd "$JITTER_ROOT/dist" && shasum -a 256 "$JITTER_ZIP" > "$JITTER_ZIP.sha256")
printf 'Packaged %s\n' "$JITTER_ROOT/dist/$JITTER_ZIP"
