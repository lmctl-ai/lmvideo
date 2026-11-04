# Third-party notices

The public README, docs, pictures and `.lms` examples are MIT-licensed under `LICENSE-examples`. Binary releases are licensed under `LICENSE`. Third-party components retain their own terms; neither project license changes an upstream grant.

This notice identifies third-party material included in the distributed host and its rendering backends. Exact license texts and notices are shipped in the adjacent `docs/third-party-licenses/` bundle and in public documentation under `docs/third-party-licenses/`.

## DejaVu Sans

The host embeds DejaVu Sans, based on Bitstream Vera. Its complete license and attribution are in [`docs/third-party-licenses/DejaVuSans-LICENSE.txt`](docs/third-party-licenses/DejaVuSans-LICENSE.txt). Upstream: <https://dejavu-fonts.github.io/>.

## lmscript runtime header

The host includes the unmodified lmcode-cpp runtime header under MIT. Its license text is in [`docs/third-party-licenses/lmcode-cpp-MIT.txt`](docs/third-party-licenses/lmcode-cpp-MIT.txt). The lmscript compiler is a separate build-time tool and is not bundled.

## Skia and its codecs

The host uses the JetBrains skia-pack `m144-e2e6623374-4` release for Canvas raster/vector operations, SVG parsing, fonts and image codecs. The bundled CPU profile carries these exact license texts and notices: Skia BSD-3-Clause (`docs/third-party-licenses/Skia.txt`); ICU (`docs/third-party-licenses/ICU.txt`); Expat (`docs/third-party-licenses/Expat.txt`); libjpeg-turbo/IJG (`docs/third-party-licenses/libjpeg-turbo.md`, `docs/third-party-licenses/README.ijg`); libpng (`docs/third-party-licenses/libpng-LICENSE`); zlib (`docs/third-party-licenses/zlib-LICENSE`); HarfBuzz (`docs/third-party-licenses/HarfBuzz-COPYING`); WebP (`docs/third-party-licenses/libwebp-COPYING`, `docs/third-party-licenses/libwebp-PATENTS`); FreeType (`docs/third-party-licenses/FreeType-FTL.TXT`); and DejaVu Sans. ANGLE, SwiftShader and Vulkan GPU components are not used by the CPU profile.

## CPU Cycles and image I/O

The Cycles-enabled CPU profile uses standalone Cycles, Embree, Open Image Denoise and its separately licensed weights, OpenImageIO, and selected image/colour/threading dependencies. [`docs/third-party-licenses/cycles/manifest.json`](docs/third-party-licenses/cycles/manifest.json) records each component's pinned version or commit, upstream source, license identifier, release role, exact license-text path and SHA-256. All matching texts are included in `docs/third-party-licenses/cycles/`.

| Component | Version | License / included notice |
| --- | --- | --- |
| Cycles | 5.3.0 | Apache-2.0 (`Cycles-LICENSE`) |
| Embree | 4.4.1 | Apache-2.0 (`Embree-LICENSE.txt`) |
| Open Image Denoise | 2.5.0 | Apache-2.0 (`OIDN-LICENSE.txt`) |
| OIDN weights | pinned model commit | Apache-2.0 (`OIDN-weights-LICENSE.txt`) |
| OpenImageIO / LibTIFF | 3.1.16.0 / 4.7.0 | Apache-2.0 / LibTIFF license |
| oneTBB | 2022.3.0 | Apache-2.0 and its bundled program notices |
| OpenColorIO / OpenEXR / Imath | 2.5.0 / 3.4.10 / 3.2.2 | BSD-3-Clause; OpenEXR patent notice included |
| fmt / pugixml / robin-map | pinned versions | MIT |
| zlib / libpng / libjpeg-turbo | pinned versions | zlib / libpng / IJG, BSD-3-Clause and zlib terms |
| OpenJPEG / OpenJPH | 2.5.3 / 0.25.2 | BSD-2-Clause |
| WebP / Zstandard / Brotli | pinned versions | BSD-3-Clause plus WebM patent grant / BSD-3-Clause option / MIT |
| ARM64 SSE2NEON | pinned commit | MIT |
| Cycles sky models | pinned Cycles source | File-level BSD-3-Clause, MIT and Apache-2.0 credits in `cycles/sky/ATTRIBUTIONS.md` |

The OpenImageIO profile excludes HEIF, FFmpeg, RAW, ImageMagick, OpenVDB and JXL codecs. OIDN uses the CPU source build and the separately licensed Apache-2.0 weights; no Intel prebuilt OIDN binary agreement is used. The pinned CPU profile excludes GPU/GUI components and unrelated packages in the Blender dependency archive.

FFmpeg and ffprobe are separate executables selected by the user and are not bundled; their distribution terms depend on that build. Operating-system runtime components retain their own licenses.

## Supplemental dependency notices

The bundle includes the complete upstream OIDN, Embree and OpenImageIO third-party
program lists. These lists may also describe optional upstream components that
the selected CPU profile does not enable; the component manifest identifies the
selected runtime dependencies. OpenColorIO's selected yaml-cpp, pystring and
minizip-ng notices are pinned in
[`opencolorio-dependency-notices.json`](docs/third-party-licenses/cycles/opencolorio-dependency-notices.json).

This software is based in part on the work of the Independent JPEG Group.
The FreeType Project (www.freetype.org) supplies the font engine under its
FreeType License; its copyright and terms are included in the bundle.

### Bundled GNU C++ runtime

Linux runtime bundles include libstdc++ and libgcc under GPL with the GCC Runtime Library Exception. Exact distributor copyright and exception texts, and GPL-3.0,
are included under `licenses/platform-runtime/`. The system's glibc and dynamic
loader are not redistributed. These upstream terms remain separate from the
project's binary license. Corresponding GCC sources are available through the
distributor/source references preserved in those copyright files.
