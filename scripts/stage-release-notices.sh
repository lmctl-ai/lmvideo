#!/usr/bin/env bash
# Copies everything a binary release must carry next to the executable into DEST (default dist/):
# lmvideo's license, the third-party notices, the exact license texts of the statically linked libraries
# (Skia, ICU, Expat, libjpeg-turbo from licenses/; libpng, zlib, HarfBuzz, libwebp, FreeType from the unpacked Skia pack)
# and the bundled DejaVu font license. Run after scripts/fetch-skia.sh.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:-$HERE/dist}"
EXT="$HERE/third_party/skia/third_party/externals"
mkdir -p "$DEST/licenses"
cp "$HERE/LICENSE" "$DEST/LICENSE"
cp "$HERE/THIRD_PARTY_NOTICES.md" "$DEST/THIRD_PARTY_NOTICES.md"
cp "$HERE"/licenses/* "$DEST/licenses/"
cp "$HERE"/assets/fonts/*LICENSE* "$DEST/licenses/" 2>/dev/null || cp "$HERE"/assets/fonts/LICENSE* "$DEST/licenses/DejaVu-LICENSE" 2>/dev/null || true
for pair in libpng/LICENSE:libpng-LICENSE zlib/LICENSE:zlib-LICENSE harfbuzz/COPYING:HarfBuzz-COPYING libwebp/COPYING:libwebp-COPYING freetype/docs/FTL.TXT:FreeType-FTL.TXT; do
  src="$EXT/${pair%%:*}"; [ -f "$src" ] || { echo "missing $src (run scripts/fetch-skia.sh)" >&2; exit 1; }
  cp "$src" "$DEST/licenses/${pair##*:}"
done
ls -l "$DEST" "$DEST/licenses"
