# lmvideo durable memory

What this repo is: bindings for Skia (images) and the ffmpeg CLI (video) as a C++ library plus a host (`lmvideohost`) for the lmscript `_dsl_` framework, where one typed DSL
chunk owns the frame/attribute loops and the host owns surface, encoder and output (operator R23, R12, R13, R27, R28, R29). Status (2026-10-19): EXPERIMENTAL, green
(`test.sh` PASS 23 checks on the v5 mono compiler and cpp-dsl-sdk `a0bebcd`); private repo `lmctlhq/lmvideo`; no release promotion. Not lmcreator (old name collision).

Team guide: lmctl-mono `lmscript/durable-memory/dsl-framework.md`. Requirements and findings: lmctl-mono `lmscript/durable-memory/requirements/dsl-bytecode.md`.
README.md holds the quick start, the DSL findings and the decision list; docs/design.md the Phase 0 research (Skija survey, Skia/ffmpeg/SVG decisions).

| File | Fact |
| --- | --- |
| [01-purpose-and-place.md](01-purpose-and-place.md) | what lmvideo is, place in the DSL framework, requirements R12/R13/R23/R27/R28/R29 |
| [02-decisions.md](02-decisions.md) | dated decisions: Skia pack, ffmpeg pipe, SVG, font, host model, host-side type checks, S0'-S5 slices, v5 |
| [03-build-run-test.md](03-build-run-test.md) | exact commands, env vars (LMSCRIPT_BIN, LMSCRIPT_MIN, LMVIDEO_SKIA_ZIP), expected output, measurements |
| [04-gotchas.md](04-gotchas.md) | Skia zip, shared-box load, fonts, SDK pin sync, ffmpeg 4.4.2 CLI only |
| [05-open-items.md](05-open-items.md) | libav backend, GPU, other platforms, unexposed Skia APIs, Font::from_file |
