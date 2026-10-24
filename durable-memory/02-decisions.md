# Design decisions

- Skia is acquired as a pinned JetBrains skia-pack release rather than built from source. The tag, URL, SHA-256, and archive size are in `scripts/skia.lock`.
- FFmpeg is invoked as a separate process through a pipe. lmvideo does not link libav headers or ship an FFmpeg binary.
- SVG uses Skia's SVG DOM so SVG and programmatic drawing share a rasterizer. The supported SVG subset is intentionally practical rather than a browser implementation.
- DejaVu Sans is embedded for deterministic text output. Its full license is kept at `assets/fonts/LICENSE-DejaVu.txt`.
- The public header is Skia-free. Only `cpp/lmvideo.cpp` includes Skia headers.
- The host keeps argument, result, lifecycle, dimension, and encoder checks at the native boundary. The shared bytecode runtime does not know lmvideo policy.
- The schema uses host object types for collections. The DSL iterates them with `len()` and `at()`; no bytecode collection opcode is required.
- Common numeric functions belong to the lmscript runtime. lmvideo retains `mix` because it validates and interpolates colors.
- The template example uses one DSL run for setup, frame loops, template expansion, PNG output, and video finalization.
- The C++ DSL header is synchronized by `scripts/sync-sdk.sh` and pinned by `sdk.lock`. Publication of a binary must also carry the upstream runtime notice.
- DSL host flag standard (lmscript R42): make embeds `schema/lmvideo.dsl.json` in `lmvideo` (generated `schema_embedded.hpp`); the registry self-check and the bytecode hash use that embedded copy, `--dsl-schema [--json]` prints it and `--schema FILE` is gone (exit 2, no alias). `LMVIDEO_SCHEMA=FILE` at build time exists only so `test.sh` can build hosts embedding edited schemas to exercise the self-check.
