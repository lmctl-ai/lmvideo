# lmvideo

lmvideo is an experimental C++ library and lmscript host for deterministic raster drawing, SVG rendering, and short video output. The host owns the process, Skia surface, FFmpeg process, and output files; one `_dsl_` chunk configures the run and performs its frame and attribute loops.

The project is licensed under the MIT License. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the bundled DejaVu Sans font, the downloaded Skia pack, external FFmpeg, and the separately maintained DSL runtime header.

## Requirements

- Linux (x86-64, arm64) or macOS (arm64, x86-64) with a C++17 compiler, `make`, Python 3, `curl`, `unzip`, `ffmpeg` and `ffprobe` (FFmpeg is only needed to write video; images do not use it).
- Node.js and npm for the released compiler package, with released lmscript **0.2.347 or newer** on `PATH` (set `LMSCRIPT_BIN` for another executable).
- A network connection for the pinned JetBrains Skia archive of your platform, or a local copy supplied through `LMVIDEO_SKIA_ZIP`.

The build downloads `Skia-m144-e2e6623374-4-<os>-Release-<arch>.zip`, verifies the SHA-256 in [scripts/skia.lock](scripts/skia.lock), and unpacks it under the ignored `third_party/` directory. Skia is not vendored in this repository.

## Install and run

Every push to `main` is released automatically (executables `lmvideo-linux-x64`, `-linux-arm64`, `-macos-arm64`, `-macos-x64`; the Linux ones are fully static). lmscript installs the right one on first use:

```sh
npm install -g @lmctl-ai/lmctl
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmvideo/main/examples/video.lms   # writes images/video.mp4
```

Video encoding runs the `ffmpeg` on your `PATH` as a separate process; see [durable-memory/06-ffmpeg-in-process-feasibility.md](durable-memory/06-ffmpeg-in-process-feasibility.md) for the in-process option.

## Build from source

```sh
npm install -g @lmctl-ai/lmctl
scripts/fetch-skia.sh
nice make                      # build/lmvideo (STATIC=1 links a fully static Linux executable)
nice bash test.sh              # builds in a temp dir and runs 26+ checks
build/lmvideo demo build/demo.png
build/lmvideo render tests/golden/scene.svg build/scene.png
```

`build/lmvideo` is one executable: the DSL host (`--dsl FILE.dslb`, `--dsl-schema [--json]`, `--dsl-signatures`, `--version`) and the two library demos (`demo`, `render`). The test script resolves `lmscript` from `PATH`, checks the version, builds in a temporary directory, checks the library and encoder, renders the SVG and image examples, validates the schema signatures, and runs the video and template examples. It does not install packages or modify system directories.

Each example compiles and runs with no arguments (it uses the `lmvideo` on `PATH`, else the released one); with arguments it only compiles, and you run the bytecode yourself:

```sh
PATH=$PWD/build:$PATH lmscript examples/image.lms                       # compile + run, writes images/image.png
lmscript examples/video.lms build/video.dslb build/video.mp4 60 30       # compile only
build/lmvideo --dsl build/video.dslb
```

The template example iterates a host-owned collection through `len()` and `at()`, interpolates keyframes in DSL loops, and produces both MP4 and PNG output. The runtime reports output paths, frame counts, bytecode size, dispatch count, and wall time in its JSON result line.

## Public interfaces

- `cpp/lmvideo.hpp` is the Skia-free C++ value API.
- `cpp/encoder.hpp` pipes RGBA frames to the external FFmpeg executable.
- `cpp/host.cpp` implements the schema in `schema/lmvideo.dsl.json`; `cpp/main.cpp` dispatches between the host and the `demo`/`render` commands.
- `examples/` contains image, video, SVG, and template-keyframe programs.
- `scripts/fetch-skia.sh` and `scripts/skia.lock` pin the external Skia archive.
- `assets/fonts/` contains the embedded DejaVu Sans font and its complete license file.

The host schema uses schema 2 signatures, overload-ready imports, enum values, and `returns: "self"` for fluent Paint and Path setters. Common numeric functions are provided by the lmscript runtime; lmvideo keeps only the colour-specific `mix` helper.

lmvideo has no lmapi dependency. Its public contract is the C++ API, the JSON schema, and the lmscript bytecode host described above.

## Relationship to lmscript and the runtime

lmscript compiles the `_dsl_` source into the bytecode consumed by `lmvideo`. lmvideo does not include the compiler. The C++ DSL runtime header is synchronized by [scripts/sync-sdk.sh](scripts/sync-sdk.sh) and pinned by [sdk.lock](sdk.lock); refresh that pin only when the runtime dependency is intentionally updated.

The SDK deliberately leaves host policy in lmvideo: image dimensions, frame counts, encoder settings, file access, and FFmpeg failures are validated by the host. The bytecode runtime supplies loading, verification, and execution and does not add host-specific limits.

## License and provenance

Read [LICENSE](LICENSE) for lmvideo's MIT terms and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before redistributing a binary. The Skia archive is a build input rather than a tracked asset; the notice file records the exact pinned release, the license files present in the archive, and the additional notices still required for binary redistribution. FFmpeg is executed as a separate user-provided process, so its enabled codecs determine the user's FFmpeg obligations.

The compiler comes from [@lmctl-ai/lmctl](https://www.npmjs.com/package/@lmctl-ai/lmctl); the shared interpreter is [cpp-dsl-sdk](https://github.com/lmctl-ai/cpp-dsl-sdk). For CLI-as-server APIs rather than embedded library callbacks, see [lmapi](https://github.com/lmctl-ai/lmapi), [lmrust](https://github.com/lmctl-ai/lmrust), and [lmcpp](https://github.com/lmctl-ai/lmcpp).
