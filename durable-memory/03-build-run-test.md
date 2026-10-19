# Build and validation

Requirements are g++ or clang++, C++17, make, Python 3, ffmpeg, ffprobe, and lmscript 0.2.347 or newer on `PATH`. Set `LMSCRIPT_BIN` to override the compiler path and `LMVIDEO_SKIA_ZIP` to reuse a local copy of the pinned archive.

The normal sequence is:

```sh
scripts/fetch-skia.sh
nice make
nice bash test.sh
```

`test.sh` checks the compiler version, verifies `cpp/dslvm.hpp` against `sdk.lock`, builds in a temporary directory, runs the Skia and encoder tests, exercises the CLI, compares host and schema signatures, compiles all DSL examples, checks PNG and MP4 facts, and covers host error paths. It does not install packages or modify system directories.

The test script's generated files live under a temporary directory and are removed on exit. The downloaded Skia tree is ignored by Git. A clean checkout therefore needs either network access or `LMVIDEO_SKIA_ZIP` pointing to the archive whose checksum matches `scripts/skia.lock`.

The template example is the primary integration path: it renders 12 frames and a PNG from one DSL invocation, iterates a host collection, and checks deterministic pixels and video facts. Measurements in the README and prior records are observations, not performance guarantees.
