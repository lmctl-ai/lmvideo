# Build and validation

Requirements are g++ or clang++, C++17, make, Python 3, ffmpeg, ffprobe, and lmscript 0.2.347 or newer on `PATH`; Linux x64/arm64 and macOS x64/arm64 are supported (Skia pack per platform, Windows is not). Set `LMSCRIPT_BIN` to override the compiler path and `LMVIDEO_SKIA_ZIP` to reuse a local copy of the pinned archive.

The normal sequence is:

```sh
scripts/fetch-skia.sh
nice make                    # build/lmvideo (STATIC=1 for a fully static Linux executable)
nice bash test.sh
```

`test.sh` puts its freshly built `lmvideo` first on `PATH` so examples declaring `cli="lmvideo"` use it. `scripts/stage-release-notices.sh dist` collects the license texts that every binary release carries.

`test.sh` checks the compiler version, verifies `cpp/dslvm.hpp` against `sdk.lock`, builds in a temporary directory, runs the Skia, plot-pixel, and encoder tests, exercises the CLI, compares host and schema signatures, checks the embedded schema (`--dsl-schema --json` byte-identical to the file, the shared `--dsl-schema` summary, `--schema FILE` refused with exit 2), compiles all DSL examples, checks PNG and MP4 facts, and covers host error paths. It does not install packages or modify system directories.

For sources that call `Canvas.plot()`, `test.sh` compiles a temporary copy against the checked-in schema and runs that bytecode on the freshly built host. The published source keeps `cli="lmvideo" install="github:lmctl-ai/lmvideo@latest"` so `lmctl autorun` resolves the released host normally.

The test script's generated files live under a temporary directory and are removed on exit. The downloaded Skia tree is ignored by Git. A clean checkout therefore needs either network access or `LMVIDEO_SKIA_ZIP` pointing to the archive whose checksum matches `scripts/skia.lock`.

The template example is the primary integration path: it renders 12 frames and a PNG from one DSL invocation, iterates a host collection, and checks deterministic pixels and video facts. Measurements in the README and prior records are observations, not performance guarantees.

## Examples and gallery

`examples/` holds four basics (`image`, `svg`, `video`, `template`), ten lmdraw ports (`damped-oscillator`, `kepler-second-law`, `lotka-volterra`, `hohmann-transfer`, `carnot-cycle`, `lens-ray-diagram`, `rc-lowpass`, `hydrogen-spectrum`, `geometry-ellipse`, `relativity-lorentz`), four new plots (`quarterly-revenue`, `sensor-calibration`, `forecast-area`, `waveform-sampling`), and `terminal-shot`. The fourteen animated plot/port examples are 960x540, 240 frames at 30 fps, and write an MP4 plus PNG still; no-argument runs also make a GIF. `test.sh` compiles and runs all fourteen and verifies H.264, dimensions and frame count.

DSL constraints met while porting (they shape how these files are written): `#` comments only; functions cannot capture caller locals, so the shared palette is a set of `c_*()` functions repeated in each file; no arrays, no `?:`, no `atan2`/`exp`/`log` (helpers are written out per file); loop variable names are read-only and cannot be redeclared. Published pictures live in `docs/images/` (GIF and PNG) and `docs/videos/` (MP4) and are what the README gallery links.

## Portability notes

- The bundled font is embedded with `.incbin`; the path is the absolute `LMVIDEO_FONT_PATH` define from the Makefile. Do not use `-Wa,-I`: clang on macOS forwards it as a C++ include path and the libc++ headers then fail with "reference to unresolved using declaration". Mach-O needs `__TEXT,__const` and underscore-prefixed symbols (see `cpp/lmvideo.cpp`).
- `sigtimedwait` does not exist on macOS; the encoder drains its own SIGPIPE with `sigpending` + `sigwait`.
- `tests/skia_test.cpp` compares to an x86-64 golden; aarch64 gets a looser tolerance because anti-aliased edges differ slightly.
