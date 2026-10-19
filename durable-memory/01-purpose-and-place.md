# Purpose and boundaries

lmvideo wraps Skia for raster drawing and the external FFmpeg command for video encoding. It also provides `lmvideohost`, which exposes those operations through the lmscript schema in `schema/lmvideo.dsl.json`.

The native host owns the process, surface, encoder, output path, and per-run object arena. The compiled DSL chunk is a callback that configures the host, draws frames, and calls lifecycle methods. Handles are host objects and do not become bytecode collections.

The current examples cover:

- image and SVG rendering;
- a direct frame animation;
- a template-keyframe animation that iterates a host collection with `len()` and `at()`.

The public C++ API keeps Skia types behind implementation pointers, so consumers of `cpp/lmvideo.hpp` do not need Skia headers. The DSL schema is the compatibility boundary for the host.

The project is experimental and Linux x86-64 focused. The released lmscript executable must be 0.2.347 or newer. See `README.md`, `docs/design.md`, and `THIRD_PARTY_NOTICES.md` for setup and redistribution details.
