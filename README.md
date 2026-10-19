# lmvideo: Skia and ffmpeg bindings for the lmscript DSL

**S1′ compiler pairing:** this checkout reads bytecode v4 only. Until a v4 compiler is released,
use `LMSCRIPT_BIN=../lmctl-mono/lmscript/bin/lmscript bash test.sh` with a built mono checkout.
Released lmscript 0.2.328 emits v3 and is deliberately refused.


Status: EXPERIMENTAL (operator brief R23; the DSL framework is R12-R22 in lmctl-mono `lmscript/durable-memory/requirements/dsl-bytecode.md`,
living language spec `lmscript/durable-memory/design/dsl-language.md`, bytecode v4). Nothing here is an accepted design. The current S4 host/schema
work is documented below; the older command transcript is retained as historical evidence for the S1/S3 host.

NOT to be confused with `lmctlhq/lmcreator` (the TypeScript media CLI, formerly named lmvideo). This repository is a new C++ project.

lmvideo is a small C++ library (`lmv::`) over Skia (images) and the ffmpeg CLI (video), plus a **DSL host** (`lmvideohost`): the host
owns the process, surface and encoder, while one lmscript `_dsl_` chunk owns the frame and attribute loops
(R12/R13). Design decisions are in [docs/design.md](docs/design.md); the findings for the lmscript team are at the end of this file.

**lmscript build needed.** `_dsl_` and `savedsl` are not in the npm staging build. Use a mono build:
`LMSCRIPT_BIN=~/repos/lmctlhq/lmctl-mono/lmscript/bin/lmscript` (default in `test.sh`: `../lmctl-mono/lmscript/bin/lmscript`).

## Quick start (a new agent can follow this verbatim)

    scripts/fetch-skia.sh                  # one-time: downloads the pinned prebuilt Skia (49.7 MB zip, 179 MB unpacked) into third_party/ (gitignored)
    nice make                              # build/liblmvideo.a, lmvideo (CLI), lmvideohost (DSL host), skia_test, encoder_test
    nice bash test.sh                      # 19 end-to-end checks (builds in a temp dir; needs ffmpeg, ffprobe, python3, LMSCRIPT_BIN)

    # CLI, no DSL
    build/lmvideo demo build/demo.png
    build/lmvideo render tests/golden/scene.svg build/scene.png --width 800

    # DSL: compile with lmscript, run with the host
    $LMSCRIPT_BIN examples/image.lms build/image.dslb build/image.png 640 360 "Hello lmvideo"
    build/lmvideohost --dsl build/image.dslb --schema schema/lmvideo.dsl.json
    $LMSCRIPT_BIN examples/video.lms build/video.dslb build/video.mp4 60 30
    build/lmvideohost --dsl build/video.dslb --schema schema/lmvideo.dsl.json
    $LMSCRIPT_BIN examples/svg.lms build/svg.dslb build/svg.png tests/golden/scene.svg
    build/lmvideohost --dsl build/svg.dslb --schema schema/lmvideo.dsl.json
    $LMSCRIPT_BIN examples/template.lms build/template.dslb build/template.mp4 build/template.png
    build/lmvideohost --dsl build/template.dslb --schema schema/lmvideo.dsl.json

Offline use: `LMVIDEO_SKIA_ZIP=/path/to/Skia-m144-e2e6623374-4-linux-Release-x64.zip scripts/fetch-skia.sh` reuses a local copy (sha256 is checked
against `scripts/skia.lock` either way). No system package is needed beyond the compiler, make, python3 and ffmpeg/ffprobe.

## What it proves

- **Skia is bindable the way Skija binds it, without a JNI layer** (Phase 1). Prebuilt JetBrains skia-pack static libraries (the same source Skija
  downloads), a Skia-free public header (`cpp/lmvideo.hpp`: `Surface`, `Canvas`, `Paint`, `Path`, `Font`, `Image`, `Svg`, `Color`) and one TU
  (`cpp/lmvideo.cpp`) that includes Skia. 77 checks: exact pixels at probe points, text ink inside the expected box, PNG/JPEG round trips,
  SVG (rect, circle, path, text), error paths, and a golden image with tolerance.
- **ffmpeg works as an encoder without libav headers** (Phase 2). `lmv::Encoder` pipes RGBA frames to the `ffmpeg` CLI (fork/exec, no shell) and
  `probe_video` / `decode_frame` read back facts with ffprobe/ffmpeg. 32 checks, including a lossless FFV1 round trip where decoded frames are
  bit-identical to the drawn frames, and h264 frame count, duration and pixels.
- **The DSL drives drawing and animation** (S4 current schema). `schema/lmvideo.dsl.json` declares 12 object types, 69 methods, 10 host functions, and 3 context names.
  `cpp/host.cpp` implements four examples: image, video, SVG, and template keyframes. The template walks a host-owned collection with `len()`/`at()` and
  renders both an MP4 and a PNG from one DSL invocation.
- **One run, two possible outputs.** The DSL configures the host, calls `video.begin()`, owns nested frame/attribute loops through `video.frame()`, and calls
  `video.end()`. The host result includes `bytecode_bytes`, dispatched `instructions`, and `wall_ms` alongside output, dimensions, frame count, and fps.

## Historical transcript (`nice bash test.sh`, 2026-10-18; S1/S3 host)

    ok 1 - cpp/dslvm.hpp matches sdk.lock (aebac89e0736)
    ok 2 - library, CLI, tests and DSL host build
    ok 3 - skia_test: PASS 77 checks
    ok 4 - lmvideo demo writes a valid 500x350 PNG
    ok 5 - lmvideo render: SVG -> 200x120 PNG
    ok 6 - CLI exit codes: 1 usage, 3 runtime error
    ok 7 - encoder_test: PASS 32 checks
    ok 8 - registry signatures equal the schema's (83 entries, lmscript --dsl-signatures)
    ok 9 - image.lms: 640x360 PNG with the sun, gradient and translucent card at the expected pixels
    ok 10 - image.lms: a second run gives identical pixels (deterministic, no system fonts)
    ok 11 - image.lms: ${} literals (size 200x100, title, path) reach the host
    ok 12 - svg.lms: SVG fitted into a rect on a canvas the chunk also draws on
    ok 13 - video.lms: mp4 is h264 320x180, 60 frames, 2.0 s        (ffprobe: codec_name=h264 width=320 height=180 nb_read_frames=60 duration=2.000000)
    ok 14 - video.lms: frame 0 and frame 59 decode to the expected ball position and colours
    ok 15 - seven ill-typed chunks are rejected at lmscript check time with located errors
    ok 16 - host refuses a schema-hash mismatch and a truncated file (one JSON line, phase load, exit 3, nothing drawn)
    ok 17 - host run-time errors: bad colour, drawing in setup, size in frame, missing output, image with 2 frames, bad size (phase run, RUN_HOST, exit 3); out.fail (phase app, APP_FAIL, exit 4)
    ok 18 - hand-built bytecode passing a Path as a Paint is stopped by the host's schema type check (exit 3, no crash)
    ok 19 - the host refuses to start when the schema has a method its registry lacks or declares with other types (registry/schema self-check, LOAD_UNBOUND, exit 3)
    ok 20 - NaN colour factor and an unwritable image path are errors (exit 3); out.fail during a video leaves no partial file
    PASS 20 checks

## The DSL chunk (examples/video.lms, the body)

    video.size(320, 180); video.fps(${fps}); video.frames(${frames}); video.output(${mp4});
    video.begin();
    for (f in range(0, video.count())) {       # DSL-owned frame loop
      video.frame();
      let w = float(video.width());  let h = float(video.height());
      let t = video.t();  let e = ease(t);
      canvas.clear(mix("#0b132b", "#5bc0be", e));
      let x = lerp(40.0, w - 40.0, e);
      let y = h * 0.6 - abs(sin(t * 6.2831853 * 2.0)) * h * 0.35;
      canvas.circle(x, y, 18.0 + 10.0 * t, paint().color(mix("#ffd166", "#ef476f", t)));
      canvas.rect(20.0, h - 24.0, (w - 40.0) * t, 8.0, paint().color("#ffffff"));
      canvas.text("frame " + str(video.index() + 1) + "/" + str(video.count()), 20.0, 30.0, font(18.0), paint().color("#ffffff"));
    }
    video.end();

## How the host runs a chunk (R12)

1. Register the schema's implementation (`register_schema`, each signature declared with its types so `lmvideohost --dsl-signatures` can be diffed
   against `lmscript --dsl-signatures schema/lmvideo.dsl.json`; the Registrar's own self-check compares the declarations with the schema file too),
   load and verify the bytecode against the exact schema bytes (refused: one JSON line, phase load, exit 3, nothing ran).
2. **Begin**: `video.begin()` creates the host-owned surface and encoder after `size`, `fps`, `frames`, `crf`, and `output` are configured.
3. The DSL calls `video.frame()` once per iteration. It clears the surface (black for video, transparent for images), exposes `index()` and `t()`, and the DSL draws. The host pipes each completed frame to the encoder; `video.end()` finalizes it. Every handle the chunk creates lives in one run arena (R11/R12).
4. Report: the human line on stderr and exactly one JSON result line on stdout (spec v4 S0, A03): `{"ok": true, "result": {"output": [every
   out.print line], "file": ..., "width": ..., "height": ..., "frames": ..., "fps": ..., "bytecode_bytes": ..., "instructions": ..., "wall_ms": ...}}`, or `{"ok": false, "phase": "load|run|app", "code": ...,
   "message": ...}`. Exit 0 ok, 2 usage, 3 host (load refused or run-time error; the message names `Type.method`), 4 the chunk called `out.fail(msg)`.
   lmvideo keeps its own call-time argument/result checks on top of the SDK (the SDK itself has none: R33/R34).

## Files

| File | Role |
| --- | --- |
| `cpp/lmvideo.hpp`, `cpp/lmvideo.cpp` | the library: Skia-free value-class API, the only TU that includes Skia; embedded DejaVu Sans via `.incbin`; SVG through `SkSVGDOM` |
| `cpp/lmvideo_cli.cpp` | CLI `lmvideo render IN.svg OUT.png [--width W] [--height H] [--background C]`, `lmvideo demo OUT.png` |
| `cpp/encoder.hpp`, `cpp/encoder.cpp` | `lmv::Encoder` (ffmpeg pipe), `probe_video`, `decode_frame`; Skia-free |
| `cpp/host.cpp`, `cpp/schema.hpp` | the DSL host (registry = implementation of the schema, begin/frame/end lifecycle, DSL-owned loops, keyframe/template objects) and its schema reader (signature checks, registry self-check) |
| `cpp/dslvm.hpp`, `sdk.lock`, `scripts/sync-sdk.sh` | shared runtime vendored from [lmctlhq/cpp-dsl-sdk](https://github.com/lmctlhq/cpp-dsl-sdk) by pinned sha256 (`test.sh` fails on drift) |
| `schema/lmvideo.dsl.json` | the contract (12 object types, 69 methods, 10 host functions; contexts `canvas`, `video`, `out`) |
| `examples/image.lms`, `video.lms`, `svg.lms`, `template.lms` | lmscript scripts (strict syntax, `_dsl_` block, `${}` literals; template emits MP4 and PNG) |
| `scripts/fetch-skia.sh`, `scripts/skia.lock` | pinned Skia acquisition (tag, URL, sha256, size) |
| `assets/fonts/` | DejaVu Sans and its licence (Bitstream Vera: redistributable with the notice) |
| `tests/skia_test.cpp`, `tests/encoder_test.cpp`, `tests/golden/` | library tests; `tools/pngpx.py` is the stdlib PNG probe used by `test.sh` |
| `docs/design.md` | Phase 0 research: Skija survey, Skia/ffmpeg/SVG decisions, schema mapping, licensing |

## Decisions (confirm or change)

1. **Skia prebuilt, not built.** JetBrains skia-pack m144-e2e6623374-4 Release x64 (49.7 MB / 179 MB), static libs, pinned by sha256. Links with g++ and clang++.
2. **ffmpeg through the CLI pipe in v1.** No libav headers on the box and none may be installed; the CLI has libx264/vp9/ffv1. `Encoder` is the seam for a libav
   backend later. Encoded bytes are never compared in tests (muxer metadata), only ffprobe facts and decoded pixels (lossless FFV1 for exactness).
3. **SVG = Skia's own `SkSVGDOM`.** Same rasterizer as the API, already in the pack. resvg stays an option behind the `Svg` class if fidelity demands it.
4. **Fonts are embedded**, never system fonts: determinism across machines. `Font(size)` is DejaVu Sans; `Font::from_file` exists in the library but is
   not exposed to the DSL yet (a path-reading function is a security decision for the lmscript team: the DSL host reads files named by the script).
5. **Colours are strings** (`"#rgb"`, `"#rrggbb"`, `"#rrggbbaa"`, a few names) because the DSL has no colour type; bad values are DSL run-time errors (exit 3).
6. **One invocation with an explicit lifecycle**: `video.begin()`, DSL-owned `for` loops calling `video.frame()`, and `video.end()` keep configuration and rendering in one chunk.
7. **Common math is a bytecode opcode** (`sin cos sqrt abs floor pow min max lerp clamp ease ease_in ease_out int`); lmvideo retains only the colour-specific `mix` helper.
8. **The Skia types never leave `lmvideo.cpp`**, so a DSL host (or any other consumer) compiles in seconds and does not need Skia headers.

## Findings for the lmscript team (Phase 4: what the DSL needs for drawing and animation)

Current evidence: four examples and the S4 schema (12 types, 69 methods, 10 host functions). The numbered transcript above is historical S1/S3 evidence;
new S4 measurements belong in the implementation verification record once the host integration is tested.

1. **The DSL-owned frame loop is now the right model for animation (R12/R29).** The host still owns the surface and encoder, but one chunk can configure,
   loop over frames and attributes, and finalize. This also makes nested loops and template keyframes ordinary DSL code.
2. **Configuration and drawing use explicit lifecycle methods.** `begin/frame/end` let the host keep its library checks and resource ownership while avoiding
   repeated callback invocations. Invalid ordering remains a host error with a clear method name.
3. **The language needs maths.** S4 places common numeric operations in a shared MATH opcode with the documented libm tolerance. lmvideo no longer registers
   duplicate math functions; `mix` remains host-owned because it validates and interpolates colours.
4. **Loops are now part of the current drawing path.** `video.lms` owns the frame loop; `template.lms` combines that with a host collection loop and
   template rendering. The old recommendation to add a loop was satisfied by S3; the old paragraph remains historical context rather than a current gap.
5. **Strings as enums and colours lose compile-time checking.** `stroke_cap("round")`, `stroke_join("miter")` and every colour are strings; a typo is caught only
   at run time and only when that branch executes (frame 37 of 60). Recommendation: schema enum types (`"enums": {"Cap": ["butt","round","square"]}`, usable as
   parameter types, literals checked by the compiler) and a `color` type that is a string subtype validated at compile time when the argument is a literal;
   `mix()` and `${}` produce runtime/pre-process strings and would keep the run-time check.
6. **Setter-returns-self is everywhere** (Paint 11, Path 9, Font 1 methods): the shorthand proposed after poc-curl (`"returns": "self"`) would shrink this schema by
   ~25 lines and remove a class of copy/paste type-name mistakes. Positional float lists are the other ergonomic cost (`canvas.round_rect(x, y, w, h, rx, ry,
   paint)` has 7 arguments); named arguments or a `Rect` literal would help, but builders (`path()`) already cover shapes, so this is low priority.
7. **Run-time errors have no source position.** A failure on frame 37 reports `Paint.color: invalid color '#12'` without the chunk line. The bytecode has no line
   table; hosts cannot say which statement failed. Recommendation: an optional debug section (pc to line) in the bytecode that `Program::run` uses to
   enrich `dslvm::Error` (and the structured error channel the POC findings already ask for).
8. **Handles as locals are fine.** `let paint = paint().color(...)` reused across calls, chains returning handles, `canvas` passed as a context object and
   `Surface.canvas()` returning another Canvas needed no new mechanism; per-run arenas clear after every frame so no handle ever outlives its run. The compiler
   accepts `let x = canvas;` (a context object in a local); harmless here, but the spec says objects cannot be stored: a decision for the spec owners.
9. **Video size and encoding settings are plain int/string setters** (`size`, `fps`, `frames`, `crf`, `output`); the host validates (even sizes for yuv420p, ranges,
   extension to codec) and reports with the method name, which matches the POC error style (exit 3). Resource limits (8192 px per side, 100000 frames,
   16384 px in the library) are host policy, as in poc-curl.
10. **The VM verifier is type-free, so the host must type-check (a security finding for the SDK).** `dslvm.hpp` verifies names and arities, never argument types,
    and the schema hash only proves WHICH schema the bytecode was compiled against. A reviewer hand-built bytecode with the correct hash that passed a `Path` where
    `Canvas.rect` wants a `Paint`; it verified and the host segfaulted (exit 139) on a `static_cast`. lmscript never emits such code, but the file format is a trust
    boundary (R03: loadable by anyone, R09 untrusted DSL). lmvideo's answer is host-side: `cpp/schema.hpp` reads the signatures, `Registrar` checks every argument and
    result against them at call time (test 17), and a startup self-check refuses to run when registry and schema disagree on names, arities or context (test 18; the
    POC finding "the host cannot check its own registry against the schema" is closed this way, ~120 lines). Recommendation: move both into `cpp-dsl-sdk` (parse the
    schema in `Program::load`, verify argument types statically from the bytecode's constant pool and a typed stack, or at least check kinds at CALL_*), so no host
    has to remember to do it.
11. **What did NOT need the DSL to change:** int/float/string/bool, `if/else`, `+` on strings and `str()` (frame counter text), method chains, handle-valued locals.
    Language v3 was sufficient for the three examples apart from the loop and maths questions above.

## Limits and not done

- Linux x86-64 only (the pinned Skia zip); Windows/macOS/arm64 archives exist in skia-pack but are not wired.
- No GPU (raster only), no `Typeface` from files in the DSL, no image filters/shaders beyond a two-colour linear gradient, no PathEffect beyond dash, no text layout
  (single line, no wrapping), no audio, no animated GIF/WebP reading. Skija surface not yet covered: `Matrix`, `Shader` objects, `ImageFilter`, `Paragraph`, `Skottie`.
- `Svg` uses SkSVG, which supports a practical subset of SVG (no scripting, limited filters/CSS). Fidelity gaps would be the trigger for a resvg path.
- A live libav integration (no process, in-memory encode) was not attempted: no headers installed.
- Rust host (R21) not attempted here: the library is C++; a Rust port would use `skia-safe` instead of this wrapper and share the schema and conformance vectors.

## Licensing

Skija (study only, Apache-2.0, no code copied), Skia (BSD-3-Clause; the pack bundles libpng, libjpeg-turbo, libwebp, FreeType, HarfBuzz, expat, zlib, ICU under their
own permissive licences: keep `third_party/skia`'s notices when distributing a binary), DejaVu Sans (Bitstream Vera licence, `assets/fonts/LICENSE-DejaVu.txt`).
ffmpeg is run as a separate process and never linked; the installed build is GPL through libx264, which only matters if a binary of ffmpeg is redistributed.

## S4 verification

`test.sh` now passes 22 checks, including the existing 77 Skia and 32 encoder checks.
The template test checks first/middle/last animation pixels, each collection-instantiated marker,
PNG pixel equality and decoded MP4 frame hashes across repeated runs. It retains host failure cleanup
and adds begin/frame/end ordering/count checks. Templates substitute `{{x}}`, `{{y}}`, `{{color}}`
(RGB) and `{{opacity}}` separately: mix() returns RGBA hex, which must not be passed directly to
Skia's SVG color parser. Handles returned by Keyframes.at() survive later add() calls (deque storage).

The measured 320×180 template example emits 12 frames at 12 fps plus a PNG in one invocation:
2220 bytecode bytes, 1758 VM dispatches, 47 ms chunk wall time on the shared box. Paths embedded in
bytecode affect its byte count; this sample uses the test's scratch paths. Wall time includes drawing,
encoding and finalization, excludes compilation/loading; dispatches exclude native callback work.
This is one observation, not a performance guarantee. The 60-frame direct canvas animation is also
checked by ffprobe and endpoint pixels. No release promotion was performed.
