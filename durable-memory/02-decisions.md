# Decisions (dated, with reasons; long forms in docs/design.md and README "Decisions")

- **2026-10-18, Skia via the pinned JetBrains skia-pack zip.** Skija consumes skia-pack; we use release `m144-e2e6623374-4`, asset
  `Skia-m144-e2e6623374-4-linux-Release-x64.zip` (49,703,089 bytes zipped, 179 MB unpacked, static libs + headers incl. `libsvg.a`).
  Pinned by sha256 in `scripts/skia.lock`; `scripts/fetch-skia.sh` unpacks into gitignored `third_party/skia`. Not built from source (gn/ninja, hours on
  the shared box, no benefit); the 632 MB Debug zip is not used. Links with g++ 11 and clang++ 14.
- **2026-10-18, ffmpeg CLI over a pipe.** ffmpeg/ffprobe 4.4.2 are installed (libx264, libvpx, ffv1) but there are no libav headers and no `-dev`
  installs are allowed. `lmv::Encoder` fork/execs ffmpeg (no shell) and writes RGBA frames to its stdin; `probe_video` / `decode_frame` read back facts.
  Prebuilt shared FFmpeg with headers was rejected (100+ MB, GPL/nonfree mixes, ABI to pin). `Encoder` is the seam for a libav backend.
- **2026-10-18, SVG via `SkSVGDOM`.** Already in the pack, same rasterizer as the canvas API, deterministic with the pinned font. resvg/tiny-skia
  (org Rust crates) stays an option behind the `Svg` class if fidelity demands it. `draw_svg(x, y, w, h)` fits honouring `preserveAspectRatio`.
- **2026-10-18, bundled DejaVu Sans.** Embedded into the binary with `.incbin` (Makefile passes `-Wa,-I$(CURDIR)`), served by Skia's FreeType custom
  font manager, hinting none, no subpixel: rendering never depends on system fonts and is identical across machines. Licence: Bitstream Vera
  (`assets/fonts/LICENSE-DejaVu.txt`).
- **2026-10-18, Skia-free public header.** Skia types never leave `cpp/lmvideo.cpp`; value classes with pimpl. Hosts compile in seconds.
- **2026-10-18, DSL host model (R12).** The host owns the process, surface, encoder, output and run arena; every handle a chunk creates dies with the
  run; colours are strings (`"#rrggbb[aa]"`, few names) because the DSL has no colour type; builders return self; constructors are host functions.
- **2026-10-18, schema type checks kept host-side.** The VM verifier checks names and arities, never types: a reviewer hand-built bytecode with the right
  schema hash that passed a `Path` as a `Paint` and segfaulted the host. `cpp/schema.hpp` + `Registrar` check every argument and result at call time
  and a startup self-check refuses to run when registry and schema disagree. These checks stay in the host (R33/R34: the SDK adds none).
- **2026-10-18, two independent model reviews fixed with tests.** Library review (9 findings: unpremultiplied readback, Font copy aliasing, eager image
  decode, SVG sizing/aspect, validation, font build dependency, golden test) and host/encoder review (type confusion, process-wide SIGPIPE, atomic
  outputs, fd leaks, leading-dash paths, strict ffprobe parsing). Outputs are written to a temp sibling and renamed, never partial.
- **2026-10-18, S0' / spec v4:** registry declares typed signatures (`--dsl-signatures` diffed against `lmscript --dsl-signatures`), host prints exactly one JSON
  result line, host failures exit 3, `out.fail` exits 4 (usage 2). Integers are full-width i64 in video conversion.
- **2026-10-18, S1' (SDK `cd7396b`):** canonical imports, context/locals/max_stack/line sections, wrapping i64 and remainder, no VM resource caps.
  Collections and domain checks remain host-owned.
- **2026-10-18, S3 (SDK `126b592`):** structured range loops with snapshot bounds and compiler-owned counters, structural termination, no runtime budget.
- **2026-10-18, S4: loops and template keyframes (R27/R28).** SDK common math replaces the host math registrations (`mix` stays a host colour operation).
  Setup-pass trick (`if (video.setup())`) was replaced: the chunk configures, calls `video.begin()`, loops `video.frame()` itself, then `video.end()`; the host
  validates the lifecycle (frame without begin, missing frames, repeated begin, extra frames are errors). `keyframes()` is a deque-backed host collection
  (`add()/len()/at()`, handles stable after append); `TemplateSvg.render(x, y, color)` instantiates an SVG string; `examples/template.lms` uses nested frame,
  attribute and collection loops and writes both mp4 and a final PNG. No execution budget was added.
- **2026-10-18, S2 schema 2:** `"dsl.schema": 2` with signature arrays; `returns: "self"` shorthand expanded by the host schema reader; this host implements singleton
  signature arrays and explicitly refuses multiple entries.
- **2026-10-18/19, DSL v5 and located errors (S5).** The host targets bytecode v5 (matching mono compiler + SDK). Division errors and `out.fail` are asserted with exact
  physical DSL line/column (test 22); existing exit codes and host checks retained. Latest SDK pin in `sdk.lock`: `a0bebcd` (S5; both runtimes pass all 50 shared
  vectors). Earlier pins (`f140fa3`, `b8fd285`) are superseded. No release promotion.
