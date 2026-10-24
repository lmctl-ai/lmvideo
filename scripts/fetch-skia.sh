#!/usr/bin/env bash
# Downloads the prebuilt Skia (JetBrains skia-pack, the same source Skija uses) into third_party/skia (gitignored).
# Pinned by release tag and the per-platform sha256 of the zip in scripts/skia.lock; idempotent.
# LMVIDEO_SKIA_ZIP=path reuses a local zip instead of downloading. LMVIDEO_PLATFORM overrides the detected platform.
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="${LMVIDEO_PLATFORM:-$("$HERE/scripts/platform.sh")}"
TAG=$(sed -n 's/^tag=//p' "$HERE/scripts/skia.lock")
SHA256=$(sed -n "s/^$PLATFORM=//p" "$HERE/scripts/skia.lock")
[ -n "$SHA256" ] || { echo "no Skia pack pinned for $PLATFORM in scripts/skia.lock" >&2; exit 1; }
OS="${PLATFORM%-*}"; ARCH="${PLATFORM#*-}"
ZIP_NAME="Skia-$TAG-$OS-Release-$ARCH.zip"
URL="https://github.com/JetBrains/skia-pack/releases/download/$TAG/$ZIP_NAME"
DEST="$HERE/third_party/skia"
sum() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi; }
if [ -f "$DEST/out/Release-$PLATFORM/libskia.a" ] && [ "$(cat "$DEST/.tag" 2>/dev/null)" = "$TAG-$PLATFORM" ]; then
  [ "${1:-}" = "--quiet" ] || echo "skia $TAG ($PLATFORM) already in $DEST"; exit 0
fi
mkdir -p "$HERE/third_party/dl"
ZIP="${LMVIDEO_SKIA_ZIP:-$HERE/third_party/dl/$ZIP_NAME}"
[ -f "$ZIP" ] || { echo "downloading $URL"; curl -fL --retry 3 -o "$ZIP.part" "$URL"; mv "$ZIP.part" "$ZIP"; }
have="$(sum "$ZIP")"
[ "$have" = "$SHA256" ] || { echo "sha256 mismatch for $ZIP: $have != $SHA256" >&2; exit 1; }
rm -rf "$DEST.tmp" "$DEST"; mkdir -p "$DEST.tmp"
unzip -q "$ZIP" -d "$DEST.tmp"; mv "$DEST.tmp" "$DEST"; echo "$TAG-$PLATFORM" >"$DEST/.tag"
echo "skia $TAG ($PLATFORM) unpacked in $DEST ($(du -sh "$DEST" | cut -f1))"
