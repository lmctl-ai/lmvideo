# Gotchas and limits

- The Skia archive is downloaded and ignored, not committed. Its checksum and platform are fixed by `scripts/skia.lock`.
- The current build targets Linux x86-64. Other skia-pack archives exist but are not wired into the Makefile.
- The embedded font is deterministic and must remain paired with its license file.
- FFmpeg is an external executable. Codec availability, FFmpeg licensing, and encoder diagnostics come from the user's installation.
- MP4/YUV420 output requires even dimensions. The host validates dimensions, frame counts, output extensions, and lifecycle ordering.
- SkSVG supports a practical SVG subset without scripting or full browser CSS. Complex documents may require a future rasterizer backend.
- The host performs its own argument and result checks because the shared runtime deliberately leaves host policy at the host boundary.
- The native object arena lasts for one DSL run. Handles cannot outlive the run or be persisted by the bytecode.
- `scripts/sync-sdk.sh` checks the vendored C++ runtime against `sdk.lock`; update both only as one dependency change.
