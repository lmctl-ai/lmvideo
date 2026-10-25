# lmvideo durable memory

`lmvideo` is a static, high-performance lmscript `_dsl_` host embedding Google Skia for 2D graphics, raster drawing, canvas geometry, chart plotting, explicit layout management, and MP4/GIF video rendering.
Repository: https://github.com/lmctl-ai/lmvideo.

## 60-second takeover

```sh
scripts/fetch-skia.sh                 # Download pinned Skia prebuilt libraries
make -j4                              # Build lmvideo binary
./test.sh                             # Run tests, golden pixel checks, examples
```

## Document map

| File | Contents |
| --- | --- |
| [01-purpose-and-place.md](01-purpose-and-place.md) | Architectural role, Skia embedding, and video rendering scope |
| [02-decisions.md](02-decisions.md) | Design decisions, Skia bindings, memory ownership, and video pipelines |
| [03-build-run-test.md](03-build-run-test.md) | Build commands, test runner, golden image checks, and CI |
| [04-gotchas.md](04-gotchas.md) | Gotchas, Skia resource lifetimes, font fallback, determinism |
| [05-open-items.md](05-open-items.md) | Technical roadmap, deferred features, and backlog |
| [06-ffmpeg-in-process-feasibility.md](06-ffmpeg-in-process-feasibility.md) | Analysis of in-process libav vs CLI ffmpeg execution |
| [07-plot-proposal.md](07-plot-proposal.md) | High-level 2D plotting APIs, series types, and styling |
| [08-layout-managers.md](08-layout-managers.md) | Explicit Tk/Swing-style container layout managers (pack, grid, border) |
| [09-plot-chart-kinds-proposal.md](09-plot-chart-kinds-proposal.md) | Typed schema and implementation facts for field, statistical, symlog, and date charts |
| [10-vector-recording.md](10-vector-recording.md) | Pinned Skia vector modules and the typed Document/Picture/PDF output API |
| [11-diagram-vocabulary.md](11-diagram-vocabulary.md) | lmdraw-compatible diagram shapes, anchored connectors, grids, and sequence notation |

## SDK pin and sync

The vendored SDK header is pinned via `sdk.lock` pointing to `https://github.com/lmctl-ai/lmcode-cpp`:
```sh
scripts/sync-sdk.sh                   # Refetches pinned cpp/dslvm.hpp and verifies sha256
```

## Adding an example or function

### Adding an example
1. Create `examples/<name>.lms` using `_dsl_ cli="lmvideo" install="github:lmctl-ai/lmvideo@latest"`.
2. Generate its rendered output: `lmscript examples/<name>.lms <output.png>`.
3. Add the example to `test.sh` with golden pixel comparison checks.
4. Save the image to `docs/images/<name>.png` and update `README.md` demo table with `lmctl autorun` command.

### Adding a host function or layout feature
1. Declare the method/function in the DSL schema (`schema/lmvideo.dsl.json`).
2. Implement and register the method in C++ source under `src/` using `dslvm::Registry`.
3. Verify that `./test.sh` passes and signatures match `lmscript --dsl-signatures`.

## Releases

Releases are published automatically on push across Linux and macOS platforms. Artifacts include standalone host binaries with Skia statically linked.

Compiler >=0.2.420 resolves std from the pinned SDK bundle by default; host schema bytes remain host-only, with full and `--without-std` parity plus a standard-only runtime probe in `test.sh`.
