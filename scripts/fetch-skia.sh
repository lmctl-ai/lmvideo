#!/usr/bin/env bash
# Downloads the prebuilt Skia (JetBrains skia-pack, the same source Skija uses) into third_party/skia (gitignored).
# Pinned by release tag and sha256 of the zip; idempotent. LMVIDEO_SKIA_ZIP=path reuses a local copy instead of downloading.
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG=m144-e2e6623374-4
ZIP_NAME="Skia-$TAG-linux-Release-x64.zip"
URL="https://github.com/JetBrains/skia-pack/releases/download/$TAG/$ZIP_NAME"
SHA256=$(sed -n 's/^sha256=//p' "$HERE/scripts/skia.lock")
DEST="$HERE/third_party/skia"
if [ -f "$DEST/out/Release-linux-x64/libskia.a" ] && [ "$(cat "$DEST/.tag" 2>/dev/null)" = "$TAG" ]; then
  [ "${1:-}" = "--quiet" ] || echo "skia $TAG already in $DEST"; exit 0
fi
mkdir -p "$HERE/third_party/dl"
ZIP="${LMVIDEO_SKIA_ZIP:-$HERE/third_party/dl/$ZIP_NAME}"
[ -f "$ZIP" ] || { echo "downloading $URL (~50 MB)"; curl -fL --retry 3 -o "$ZIP.part" "$URL"; mv "$ZIP.part" "$ZIP"; }
have="$(sha256sum "$ZIP" | cut -c1-64)"
[ "$have" = "$SHA256" ] || { echo "sha256 mismatch for $ZIP: $have != $SHA256" >&2; exit 1; }
rm -rf "$DEST.tmp" "$DEST"; mkdir -p "$DEST.tmp"
unzip -q "$ZIP" -d "$DEST.tmp"; mv "$DEST.tmp" "$DEST"; echo "$TAG" >"$DEST/.tag"
echo "skia $TAG unpacked in $DEST ($(du -sh "$DEST" | cut -f1))"
