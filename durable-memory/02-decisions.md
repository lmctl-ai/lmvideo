# Design decisions

- Skia is acquired as a pinned JetBrains skia-pack release rather than built from source. The tag and one SHA-256 per platform (linux x64/arm64, macOS x64/arm64) are in `scripts/skia.lock`; `scripts/platform.sh` picks the platform.
- FFmpeg is invoked as a separate process through a pipe. lmvideo does not link libav headers or ship an FFmpeg binary. Linking libav in process is evaluated in `06-ffmpeg-in-process-feasibility.md` (feasible, LGPL policy pending).
- SVG uses Skia's SVG DOM so SVG and programmatic drawing share a rasterizer. The supported SVG subset is intentionally practical rather than a browser implementation.
- DejaVu Sans is embedded for deterministic text output. Its full license is kept at `assets/fonts/LICENSE-DejaVu.txt`.
- The public header is Skia-free. Only `cpp/lmvideo.cpp` includes Skia headers.
- The host keeps argument, result, lifecycle, dimension, and encoder checks at the native boundary. The shared bytecode runtime does not know lmvideo policy.
- The schema uses host object types for collections. The DSL iterates them with `len()` and `at()`; no bytecode collection opcode is required.
- Common numeric functions belong to the lmscript runtime. lmvideo retains `mix` because it validates and interpolates colors.
- The template example uses one DSL run for setup, frame loops, template expansion, PNG output, and video finalization.
- The C++ DSL header is synchronized by `scripts/sync-sdk.sh` and pinned by `sdk.lock`. Publication of a binary must also carry the upstream runtime notice.
- DSL host flag standard (lmscript R42): make embeds `schema/lmvideo.dsl.json` in `lmvideo` (generated `schema_embedded.hpp`); the registry self-check and the bytecode hash use that embedded copy, `--dsl-schema [--json]` prints it and `--schema FILE` is gone (exit 2, no alias). `LMVIDEO_SCHEMA=FILE` at build time exists only so `test.sh` can build hosts embedding edited schemas to exercise the self-check.

- One executable (2026-10-24): `cpp/main.cpp` dispatches `--version`, `demo` and `render` (library demos, `lmvideo_cli.cpp`) and everything else to the DSL host (`host.cpp`), so lmscript's `cli="lmvideo" install="github:lmctl-ai/lmvideo@latest"` finds a host named `lmvideo`. Release assets are `lmvideo-<os>-<arch>`; Linux builds are fully static (`make STATIC=1`), macOS links Skia with the system frameworks. Examples compile only when given arguments and compile-and-run when given none.
- Releases are automatic: every push to `main` is built and tested on four platforms and published as `v<MAJOR.MINOR from VERSION>.<run number>`; CI writes that version into `VERSION` before compiling (`scripts/stamp-version.sh`), and the executable reports it (`lmvideo --version`).
