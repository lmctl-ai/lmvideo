# Build, run, test

Toolchain (checked 2026-10-18): g++ 11.4, clang++ 14, make, python3 (stdlib only, run with `-I`), ffmpeg + ffprobe 4.4.2 CLI, node (lmscript). No fontconfig, no
libav headers, no Skia installed system-wide. Linux x86-64 only. NO sudo/apt installs.

Env vars: `LMSCRIPT_BIN` (mono build of lmscript; default `../lmctl-mono/lmscript/bin/lmscript`; `_dsl_`/`savedsl` are not in the npm staging build and release
0.2.328 emits v3, which this checkout refuses), `LMSCRIPT_MIN=0` (sibling DSL repos' `test.sh` use it until a matching release: lmscript's minimum-version gate;
lmvideo's test.sh does not set it), `LMVIDEO_SKIA_ZIP=/path/to/Skia-...zip` (reuse a local zip instead of downloading; sha256 still checked), `TMPDIR`, `CXX`,
`BUILD` (make output dir, default `build`).

    scripts/fetch-skia.sh                 # once: pinned prebuilt Skia into third_party/skia (idempotent; --quiet)
    nice make                             # build/liblmvideo.a lmvideo lmvideohost skia_test encoder_test
    LMSCRIPT_BIN=~/repos/lmctlhq/lmctl-mono/lmscript/bin/lmscript nice bash test.sh   # builds in a temp dir; ends with "PASS 23 checks"
    build/lmvideo demo build/demo.png
    build/lmvideo render tests/golden/scene.svg build/scene.png --width 800
    $LMSCRIPT_BIN examples/template.lms build/template.dslb build/template.mp4 build/template.png
    build/lmvideohost --dsl build/template.dslb --schema schema/lmvideo.dsl.json     # stdout: ONE JSON result line
    build/lmvideohost --dsl-signatures                                                # registry signatures, diffed by test.sh against lmscript --dsl-signatures

Examples (all `${}`-parameterised, see header comments): `image.lms` (poster), `video.lms` (60 frames, 2.0 s h264), `svg.lms` (SVG file fitted on a canvas),
`template.lms` (12 frames, nested loops, host collection, template SVG, mp4 + png).

Expected (2026-10-19, SDK a0bebcd): `test.sh` PASS 23 checks: dslvm pin matches; build; `skia_test` PASS 77; CLI demo/render/exit codes; `encoder_test` PASS 32;
registry == schema (82 signature entries); image pixels + identical rerun + `${}` reach host; SVG; video ffprobe `codec_name=h264 width=320 height=180
nb_read_frames=60 duration=2.000000`; template; 7 compile-time rejections; load refusals; host lifecycle errors; wrong-type bytecode stopped; registry self-check;
NaN/unwritable path/no partial file; computed division by zero reports DSL line `3:11`; frame/begin lifecycle.

Result line: `{"ok": true, "result": {"output": [...], "file", "width", "height", "frames", "fps", "bytecode_bytes", "instructions", "wall_ms"}}` or
`{"ok": false, "phase": "load|run|app", "code", "message"}`. Exit 0 ok, 2 usage, 3 host (load refused or run-time error, message names `Type.method`), 4 `out.fail`.
CLI `lmvideo`: exit 1 usage, 3 runtime error.

Measurements (one shared-box observation each, not guarantees): template run 320x180, 12 frames at 12 fps + PNG: 1758 VM dispatches, 47 ms (S4 record, 2220
bytecode bytes) and 49 ms / 3072 bytes on 2026-10-19 (bytecode size depends on the scratch paths embedded and the v5 format); 60-frame direct canvas animation about
0.24 s of host time (Phase 3, v3 host). Clean-clone check: a fresh `git clone` builds and passes with a local Skia zip.
