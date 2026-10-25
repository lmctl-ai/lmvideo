# Third-party notices

Project-owned lmvideo source is licensed under the MIT License in [LICENSE](LICENSE). The notices below cover material that lmvideo includes or uses at build and runtime. lmvideo does not claim ownership of these components.

## DejaVu Sans

The repository includes `assets/fonts/DejaVuSans.ttf`, embedded into the lmvideo binary. It is DejaVu Sans, based on Bitstream Vera. Its license and attribution are preserved verbatim in [assets/fonts/LICENSE-DejaVu.txt](assets/fonts/LICENSE-DejaVu.txt).

Upstream: <https://dejavu-fonts.github.io/>. The DejaVu project identifies DejaVu changes as public domain and preserves the Bitstream Vera copyright and permission terms. Redistribution must retain the notices and must follow the font renaming condition for modified fonts. The tracked font SHA-256 is `690243adfefe0ce154519b547db6205794bd30ac4277275179517a90994f4980648` and matches the system DejaVu Sans 2.37 file used for provenance checking.

## Skia and the JetBrains skia-pack

The build downloads, but this repository does not vendor, the pinned x86-64 Linux release `m144-e2e6623374-4` from JetBrains skia-pack. The exact URL, SHA-256, and byte count are recorded in [scripts/skia.lock](scripts/skia.lock). The archive is unpacked into the ignored `third_party/skia` directory by [scripts/fetch-skia.sh](scripts/fetch-skia.sh).

Upstream archive: <https://github.com/JetBrains/skia-pack/releases/tag/m144-e2e6623374-4>. The pinned JetBrains Skia fork's inspected license text is <https://raw.githubusercontent.com/JetBrains/skia/e2e66233743d048dc880203b6598fd34d94d9765/LICENSE> and is reproduced below. The release archive itself is not tracked here; any redistributed Skia binary or static library must carry this license and the applicable dependency notices.

The exact upstream BSD-3-Clause text is retained in [licenses/Skia.txt](licenses/Skia.txt), including the 2011 Google Inc. copyright.

The extracted pinned pack was inspected. Its actual license files are `third_party/externals/angle2/LICENSE`, `libwebp/COPYING`, `harfbuzz/COPYING`, `swiftshader/LICENSE.txt`, `libpng/LICENSE`, `zlib/LICENSE`, and `include/third_party/vulkan/LICENSE`. The release also contains or links static libraries named `libexpat.a`, `libfreetype2.a`, `libharfbuzz.a`, `libicu.a`, `libjpeg*.a`, `libpng.a`, `libwebp*.a`, and `libzlib.a`. The pack does not expose a separate license file for every one of those libraries in the extracted tree. This repository therefore records provenance for source preparation only; binary redistribution requires collecting and shipping the missing upstream notices before publication.

The verified license families for the linked libraries are:

| Component | License family | Primary upstream license or notice |
| --- | --- | --- |
| ANGLE (`angle2`) | BSD 3-Clause | <https://github.com/google/angle/blob/main/LICENSE> |
| libpng | libpng license | <https://github.com/pnggroup/libpng/blob/libpng16/LICENSE> |
| libjpeg-turbo | IJG / BSD-style | <https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md> |
| libwebp | BSD 3-Clause | <https://github.com/webmproject/libwebp/blob/main/COPYING> |
| zlib | zlib License | <https://github.com/madler/zlib/blob/master/LICENSE> |
| FreeType | FreeType License / GPLv2 alternative | <https://gitlab.freedesktop.org/freetype/freetype/-/blob/master/docs/FTL.TXT> |
| HarfBuzz | MIT | <https://github.com/harfbuzz/harfbuzz/blob/main/COPYING> |
| Expat | MIT | <https://github.com/libexpat/libexpat/blob/master/expat/COPYING> |
| ICU | ICU License | <https://github.com/unicode-org/icu/blob/main/icu4c/LICENSE> |
| SwiftShader | Apache 2.0 | <https://github.com/google/swiftshader/blob/main/LICENSE.txt> |
| Vulkan headers | Apache 2.0 | <https://github.com/KhronosGroup/Vulkan-Headers/blob/main/LICENSE.txt> |

The table identifies licensing families for compatibility review; it does not replace the exact upstream notices required for binary redistribution.

### Binary redistribution

Release binaries link the Skia pack's static libraries, so each release carries the exact license texts next to the executable
(`scripts/stage-release-notices.sh`): `licenses/Skia.txt`, `ICU.txt` (Unicode License v3), `Expat.txt`, `libjpeg-turbo.md` and `README.ijg`
(IJG + BSD-3-Clause + zlib terms) from this repository, `libpng-LICENSE`, `zlib-LICENSE`, `HarfBuzz-COPYING`, `libwebp-COPYING` and
`FreeType-FTL.TXT` (FreeType used under its FTL) copied from the unpacked pack, and the DejaVu font license. ANGLE, SwiftShader and the Vulkan
headers are in the pack but not linked into the CPU-raster build.

## FFmpeg

lmvideo does not link FFmpeg or redistribute an FFmpeg binary. It starts the user-selected `ffmpeg` executable as a separate process and uses `ffprobe` for inspection. The user's FFmpeg build determines its license and codec obligations. FFmpeg's official legal page is <https://ffmpeg.org/legal.html>; the project explains that builds may be LGPL or GPL depending on enabled components, including GPL codecs such as libx264.

## Repository-generated test assets

`tests/golden/scene.svg` is authored in this repository and has no external image or font file reference. `tests/golden/scene.png` is a generated test fixture produced from that SVG by the repository's Skia renderer; its text uses the bundled DejaVu Sans font covered above. These files do not add a separate third-party asset or attribution requirement.

## lmscript and the DSL runtime

The lmvideo host consumes lmscript compiler output and vendors the C++ DSL runtime header `cpp/dslvm.hpp` from
[lmcode-cpp](https://github.com/lmctl-ai/lmcode-cpp) (MIT License, Copyright (c) 2026 Lattice Systems LLC, the same terms as lmvideo itself),
pinned by [sdk.lock](sdk.lock) and synchronized by [scripts/sync-sdk.sh](scripts/sync-sdk.sh). The header is unmodified. lmscript itself is a
separate build-time dependency and is not included in this repository.
