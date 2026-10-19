# lmvideo: Skia and ffmpeg bindings for the lmscript DSL

Status: EXPERIMENTAL (operator brief R23; the DSL framework is R12-R22 in lmctl-mono `lmscript/durable-memory/requirements/dsl-bytecode.md`,
living language spec `lmscript/durable-memory/design/dsl-language.md`, bytecode v4). Nothing here is an accepted design. Built and tested
on 2026-10-18 with g++ 11, Skia m144 (prebuilt), ffmpeg 4.4.2 and a mono build of lmscript.

NOT to be confused with `lmctlhq/lmcreator` (the TypeScript media CLI, formerly named lmvideo). This repository is a new C++ project.

lmvideo is a small C++ library (`lmv::`) over Skia (images) and the ffmpeg CLI (video), plus a **DSL host** (`lmvideohost`): the host
owns the process, the output file and the frame loop, and an lmscript `_dsl_` chunk is the callback that draws each frame
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
- **The DSL drives drawing and animation** (Phase 3). `schema/lmvideo.dsl.json` (9 object types, 60 methods, 20 functions, 3 context names) is implemented by
  `cpp/host.cpp`; three lmscript examples compile against it with `${}` literals for size, colours, title, frame count, fps and paths.
- **One run, one file.** `image.lms` produces a poster, `video.lms` a 2-second mp4 (60 frames) in 0.24 s of host time, `svg.lms` composes an SVG
  document with DSL drawing. Rendering is deterministic (a second run gives identical pixels): no system fonts are used, DejaVu Sans is embedded.

## Transcript (`nice bash test.sh`, 2026-10-18)

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

    if (video.setup()) {                       # SETUP pass: declare what the host needs to know
      video.size(320, 180); video.fps(${fps}); video.frames(${frames}); video.output(${mp4});
    } else {                                   # FRAME pass: the host runs this once per frame
      let w = float(video.width());  let h = float(video.height());
      let t = video.t();  let e = ease(t);
      canvas.clear(mix("#0b132b", "#5bc0be", e));
      let x = lerp(40.0, w - 40.0, e);
      let y = h * 0.6 - abs(sin(t * 6.2831853 * 2.0)) * h * 0.35;
      canvas.circle(x, y, 18.0 + 10.0 * t, paint().color(mix("#ffd166", "#ef476f", t)));
      canvas.rect(20.0, h - 24.0, (w - 40.0) * t, 8.0, paint().color("#ffffff"));
      canvas.text("frame " + str(video.index() + 1) + "/" + str(video.count()), 20.0, 30.0, font(18.0), paint().color("#ffffff"));
    }

## How the host runs a chunk (R12)

1. Register the schema's implementation (`register_schema`, each signature declared with its types so `lmvideohost --dsl-signatures` can be diffed
   against `lmscript --dsl-signatures schema/lmvideo.dsl.json`; the Registrar's own self-check compares the declarations with the schema file too),
   load and verify the bytecode against the exact schema bytes (refused: one JSON line, phase load, exit 3, nothing ran).
2. **Setup pass**: run the chunk once with `video.setup() == true` and an empty canvas slot. The chunk declares `size`, `fps`, `frames`, `crf`, `output`.
3. The host creates the surface (and the ffmpeg encoder when the output extension is `.mp4/.webm/.mkv/.gif`; `.png/.jpg` means a single image and needs
   `frames(1)`), then **owns the loop**: per frame it clears the surface (black for video, transparent for images), runs the chunk, and pipes the RGBA
   frame to the encoder. Every handle the chunk created lives in an arena that is cleared after each run (R11/R12: handles cannot be stored or returned).
4. Report: the human line on stderr and exactly one JSON result line on stdout (spec v4 S0, A03): `{"ok": true, "result": {"output": [every
   out.print line], "file": ..., "width": ..., "height": ..., "frames": ..., "fps": ...}}`, or `{"ok": false, "phase": "load|run|app", "code": ...,
   "message": ...}`. Exit 0 ok, 2 usage, 3 host (load refused or run-time error; the message names `Type.method`), 4 the chunk called `out.fail(msg)`.
   lmvideo keeps its own call-time argument/result checks on top of the SDK (the SDK itself has none: R33/R34).

## Files

| File | Role |
| --- | --- |
| `cpp/lmvideo.hpp`, `cpp/lmvideo.cpp` | the library: Skia-free value-class API, the only TU that includes Skia; embedded DejaVu Sans via `.incbin`; SVG through `SkSVGDOM` |
| `cpp/lmvideo_cli.cpp` | CLI `lmvideo render IN.svg OUT.png [--width W] [--height H] [--background C]`, `lmvideo demo OUT.png` |
| `cpp/encoder.hpp`, `cpp/encoder.cpp` | `lmv::Encoder` (ffmpeg pipe), `probe_video`, `decode_frame`; Skia-free |
| `cpp/host.cpp`, `cpp/schema.hpp` | the DSL host (registry = implementation of the schema, setup pass, frame loop) and its small schema reader (signature checks, registry self-check) |
| `cpp/dslvm.hpp`, `sdk.lock`, `scripts/sync-sdk.sh` | shared runtime vendored from [lmctlhq/cpp-dsl-sdk](https://github.com/lmctlhq/cpp-dsl-sdk) by pinned sha256 (`test.sh` fails on drift) |
| `schema/lmvideo.dsl.json` | the contract (types Paint, Path, Font, Image, Svg, Canvas, Surface, Video, Output; functions; context `canvas`, `video`, `out`) |
| `examples/image.lms`, `video.lms`, `svg.lms` | lmscript scripts (strict syntax, `_dsl_` block, `${}` literals) |
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
6. **Setup pass in the same chunk** (`if (video.setup()) {...} else {...}`): the only way today to let a single chunk both configure and draw (see findings 1).
7. **Math is host functions** (`sin cos sqrt abs floor pow min max lerp clamp ease mix to_int`) because the language has only `+ - * /` (findings 2).
8. **The Skia types never leave `lmvideo.cpp`**, so a DSL host (or any other consumer) compiles in seconds and does not need Skia headers.

## Findings for the lmscript team (Phase 4: what the DSL needs for drawing and animation)

Evidence: the three examples, the schema (9 types, 60 methods, 20 functions), 7 compile-time and 10 run-time negative tests, and two independent model reviews (library and host) whose findings are fixed with tests.

1. **The host-owned frame loop works and is the right model (R12).** The chunk is a pure per-frame function of `video.t()`/`video.index()`;
   the host does clear, run, encode 60 times in 0.24 s total (VM overhead is negligible next to rasterization and x264). The DSL needed no loop to animate.
   Recommendation: keep the host-owned loop; if animations need state across frames (physics, particles), give the host a persistent `State` object
   (set/get floats by name) rather than letting the chunk keep variables.
2. **Config and per-frame code need two callbacks.** One chunk per schema forced the `video.setup()` pass: the same bytecode runs once to declare
   size/fps/frames/output and then per frame, and the compiler cannot stop a chunk from drawing during setup or declaring `size` during a frame (host
   run-time errors 3 do, covered by tests r2/r3). The POC finding "named callbacks or several blocks per schema" is now confirmed by a second domain.
   Recommendation: schema-declared callbacks with their own context, e.g. `"callbacks": {"setup": {"context": {"video": "VideoSetup"}}, "frame":
   {"context": {"canvas": "Canvas", "video": "Frame"}}}`, and two `_dsl_` blocks (or `fn setup() {...}`-like sections) in one file; then calling `canvas.*` in setup
   is a compile-time error instead of a run-time one.
3. **The language needs maths.** Drawing and animation immediately need `sin`, `cos`, `sqrt`, `abs`, `floor`, `pow`, `min`, `max`, `lerp`, `clamp`, easing and
   `float -> int` (`to_int`). These are 12 of the 20 host functions here; every future numeric host (poc-numpy, lmor) will want the same set. Recommendation:
   put a fixed math library in the language's common part (like `float()` and `str()`), with the exact semantics specified for conformance (IEEE, `sin`
   accuracy is libm-dependent: specify "within 1 ulp" or restrict to deterministic functions), and add the `int(f)` narrowing with explicit rounding names
   (`floor`/`round`/`trunc`) instead of `to_int` ad hoc. `${3.0}` becoming the int `3` was visible again: `float(video.width())` is needed whenever an int size meets float geometry.
4. **Loops are the first real gap for drawing; this project did not need one but the next scene will.** A grid of 10x10 circles, a bar chart of N values or a
   particle burst need repeated statements; lmscript cannot unroll them (`${}` only substitutes literals, it does not generate code). Within the examples
   the workaround is a host method that draws the repeated thing (`Path`) or `video.index()` driving one object per frame, which is not general. This is
   UNVERIFIED as a measured need (no failing example exists yet) but strongly expected; it feeds Q12 (config chunk R13 vs one-shot program R20).
   Recommendation: a bounded `for i in range(a, b)` with a VM step limit is the smallest addition (the lmor project may already be implementing it, R18).
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
