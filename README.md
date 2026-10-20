# lmvideo

lmvideo is an experimental C++ library and lmscript host for deterministic raster drawing, SVG rendering, and short video output. The host owns the process, Skia surface, FFmpeg process, and output files; one `_dsl_` chunk configures the run and performs its frame and attribute loops.

The project is licensed under the MIT License. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the bundled DejaVu Sans font, the downloaded Skia pack, external FFmpeg, and the separately maintained DSL runtime header.

## Requirements

- Linux x86-64 with a C++17 compiler, `make`, Python 3, `curl`, `unzip`, `ffmpeg`, and `ffprobe`.
- Node.js and npm for the released compiler package.
- Released lmscript **0.2.347 or newer** on `PATH` (0.2.346 is the minimum supported version). Set `LMSCRIPT_BIN` when using another executable.
- A network connection for the pinned JetBrains Skia archive, or a local copy supplied through `LMVIDEO_SKIA_ZIP`.

The build downloads `Skia-m144-e2e6623374-4-linux-Release-x64.zip`, verifies the SHA-256 in [scripts/skia.lock](scripts/skia.lock), and unpacks it under the ignored `third_party/` directory. Skia is not vendored in this repository.

## Build and run

```sh
npm install -g @lmctl-ai/lmctl@0.2.347
scripts/fetch-skia.sh
nice make
nice bash test.sh
build/lmvideo demo build/demo.png
build/lmvideo render tests/golden/scene.svg build/scene.png
```

The test script resolves `lmscript` from `PATH`, checks the version, builds in a temporary directory, checks the library and encoder, renders the SVG and image examples, validates the schema signatures, and runs the video and template examples. It does not install packages or modify system directories.

Compile and run the DSL examples with the released CLI:

```sh
lmscript examples/image.lms build/image.dslb build/image.png 640 360 "Hello lmvideo"
build/lmvideohost --dsl build/image.dslb
lmscript examples/video.lms build/video.dslb build/video.mp4 60 30
build/lmvideohost --dsl build/video.dslb
lmscript examples/template.lms build/template.dslb build/template.mp4 build/template.png
build/lmvideohost --dsl build/template.dslb
```

The host follows the lmctl DSL host flag standard: `make` embeds `schema/lmvideo.dsl.json` in `lmvideohost` (a generated header), and bytecode is verified against that embedded copy. `build/lmvideohost --dsl-schema --json` prints those exact bytes, `--dsl-schema` prints the shared summary (`context`/`fn` lines, then `Owner:` with indented methods) and `--dsl-signatures` the canonical signature list. There is no `--schema FILE` option; it and any other flag combination are refused with exit 2 and nothing on stdout.

The template example iterates a host-owned collection through `len()` and `at()`, interpolates keyframes in DSL loops, and produces both MP4 and PNG output. The runtime reports output paths, frame counts, bytecode size, dispatch count, and wall time in its JSON result line.

## Public interfaces

- `cpp/lmvideo.hpp` is the Skia-free C++ value API.
- `cpp/encoder.hpp` pipes RGBA frames to the external FFmpeg executable.
- `cpp/host.cpp` implements the schema in `schema/lmvideo.dsl.json` for `lmvideohost`.
- `examples/` contains image, video, SVG, and template-keyframe programs.
- `scripts/fetch-skia.sh` and `scripts/skia.lock` pin the external Skia archive.
- `assets/fonts/` contains the embedded DejaVu Sans font and its complete license file.

The host schema uses schema 2 signatures, overload-ready imports, enum values, and `returns: "self"` for fluent Paint and Path setters. Common numeric functions are provided by the lmscript runtime; lmvideo keeps only the colour-specific `mix` helper.

lmvideo has no lmapi dependency. Its public contract is the C++ API, the JSON schema, and the lmscript bytecode host described above.

## Relationship to lmscript and the runtime

lmscript compiles the `_dsl_` source into the bytecode consumed by `lmvideohost`. lmvideo does not include the compiler. The C++ DSL runtime header is synchronized by [scripts/sync-sdk.sh](scripts/sync-sdk.sh) and pinned by [sdk.lock](sdk.lock); refresh that pin only when the runtime dependency is intentionally updated.

The SDK deliberately leaves host policy in lmvideo: image dimensions, frame counts, encoder settings, file access, and FFmpeg failures are validated by the host. The bytecode runtime supplies loading, verification, and execution and does not add host-specific limits.

## License and provenance

Read [LICENSE](LICENSE) for lmvideo's MIT terms and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before redistributing a binary. The Skia archive is a build input rather than a tracked asset; the notice file records the exact pinned release, the license files present in the archive, and the additional notices still required for binary redistribution. FFmpeg is executed as a separate user-provided process, so its enabled codecs determine the user's FFmpeg obligations.

The compiler comes from [@lmctl-ai/lmctl](https://www.npmjs.com/package/@lmctl-ai/lmctl); the shared interpreter is [cpp-dsl-sdk](https://github.com/lmctl-ai/cpp-dsl-sdk). For CLI-as-server APIs rather than embedded library callbacks, see [lmapi](https://github.com/lmctl-ai/lmapi), [lmrust](https://github.com/lmctl-ai/lmrust), and [lmcpp](https://github.com/lmctl-ai/lmcpp).
