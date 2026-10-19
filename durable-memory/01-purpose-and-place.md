# Purpose and place

**What lmvideo is.** A small C++ library (`lmv::`, namespace in `cpp/lmvideo.hpp`) over Skia (2D images) and the ffmpeg CLI (video), plus a
DSL host `lmvideohost` that exposes it to the lmscript `_dsl_` framework. Skia creates images, ffmpeg encodes video. Operator brief R23
(2026-10-18); repo `lmctlhq/lmvideo` (private, branch `main`).

**Not lmcreator.** `github.com/lmctlhq/lmvideo` was the old name of `lmctlhq/lmcreator` (TypeScript media CLI) and redirected to it. This repo is a
new C++ project that took the name. Never touch lmcreator from here; its docs were read for ideas only.

**Place in the DSL framework.** Team guide: lmctl-mono `lmscript/durable-memory/dsl-framework.md`. Requirements and findings:
lmctl-mono `lmscript/durable-memory/requirements/dsl-bytecode.md`; language spec `lmscript/durable-memory/design/dsl-language.md`;
shared runtime: `lmctlhq/cpp-dsl-sdk` (`dslvm.hpp`, vendored here by pinned sha256, see `sdk.lock`).

| Requirement | What it means here |
| --- | --- |
| R12 host model | the host program is the real program: it owns process, surface, encoder, output file; the DSL runs inside the context the host hands it |
| R13 typed DSL | one strongly typed chunk; the schema (`schema/lmvideo.dsl.json`) defines objects, methods and functions; objects are used, never defined |
| R23 this project | bindings for ffmpeg and Skia; study Skija (HumbleUI, Apache-2.0, JNI over prebuilt Skia) for the API surface; maybe an SVG-like drawing format |
| R27 video needs loops | motion and attribute tweening are loops (frames x attributes); nested bounded `range` loops, float math in the language |
| R28 keyframe model | Skia/SVG draws KEYFRAMES; loops tween between them; keyframes can come from a TEMPLATE iterated over a collection |
| R29 build-large-model principle | instructions with loops, not a linear listing; program size must not grow with output size (video frames are one instance) |

**What it proves.** The DSL drives drawing and animation: one chunk configures the host, calls `video.begin()`, owns nested frame/attribute loops
through `video.frame()`, and `video.end()`; image, video, SVG and template-keyframe examples run from `${}`-parameterised lmscript files.
Status 2026-10-19: EXPERIMENTAL, green (`test.sh` PASS 23 checks, 77 Skia + 32 encoder checks inside). Nothing is released or promoted.
