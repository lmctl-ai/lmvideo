# lmvideo design

lmvideo has three layers:

1. `lmv::` value classes provide a small C++ drawing and encoding API. Skia implementation details stay in `cpp/lmvideo.cpp`; the public header does not require Skia headers.
2. `lmvideohost` registers the schema's object methods and functions, owns the native arena, and validates host policy such as dimensions, lifecycle, file output, and encoder state.
3. lmscript compiles one typed DSL chunk. The host loads and runs that chunk, which configures the run and performs frame, attribute, and template loops.

## Dependencies

The build downloads the pinned JetBrains skia-pack archive described by `scripts/skia.lock`. The archive is ignored rather than vendored. The Makefile links the raster, SVG, font, and image libraries supplied by that archive. The exact archive license files present and the additional binary redistribution work are recorded in `THIRD_PARTY_NOTICES.md`.

FFmpeg is used through its command-line interface. lmvideo writes RGBA frames to a child process and uses ffprobe for facts. No libav headers or FFmpeg binary are included.

DejaVu Sans is embedded through the assembler `.incbin` mechanism so text rendering does not depend on system fonts. The source font and complete license file are tracked under `assets/fonts/`.

## Host and schema

The schema exposes opaque objects such as `Canvas`, `Paint`, `Path`, `Image`, `Svg`, `Surface`, `Video`, and `Output`. Constructors and collections are host functions and methods. Collection iteration uses `len()` and `at()` on host objects, keeping the bytecode format small.

The video lifecycle is explicit: configure the `Video`, call `begin()`, call `frame()` once per DSL loop iteration, draw, and call `end()`. The host still owns encoder startup, frame submission, cleanup, and error reporting.

SVG parsing uses Skia's SVG DOM so SVG and canvas drawing share a rasterizer. Template SVG rendering substitutes controlled values before parsing; it does not attempt to implement a browser or a general template language.

## Reproducibility

The test script compiles the image, SVG, video, and template examples with released lmscript, checks schema signatures, validates PNG pixels and MP4 metadata, and repeats deterministic outputs. Build prerequisites, temporary output behavior, and platform assumptions are in `durable-memory/03-build-run-test.md`.
