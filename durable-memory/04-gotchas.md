# Gotchas

- **Skia zip.** 49.7 MB download from GitHub releases, 179 MB unpacked under `third_party/` (gitignored, so a fresh clone needs `scripts/fetch-skia.sh`). The sha256 and
  size live in `scripts/skia.lock`; a mismatch aborts. Zips over ~500 MB must be reported first (the Debug zip is 632 MB and is NOT the one used). Tag in
  `fetch-skia.sh` and `skia.lock` must change together. The unpacked dir is stamped with `.tag`.
- **Skia headers under g++.** They carry clang-only attributes that warn: the Makefile includes them with `-isystem` and `-w`. Static libs need `--start-group/--end-group`.
- **Embedded font.** `.incbin` needs `-Wa,-I$(CURDIR)`; the font is a prerequisite of `lmvideo.o` (changing it must rebuild). System fonts are never used; no fontconfig exists.
- **Skia API shape.** `Surface::rgba()` returns unpremultiplied RGBA (the encoder pipe expects straight alpha); `Font` copies are deep; images are decoded eagerly (truncated
  files fail at load, not at draw time); SVG intrinsic size comes from width/height/viewBox.
- **ffmpeg 4.4.2 CLI only.** No libav headers; yuv420p codecs need even width/height; output extension picks the codec (`.mp4` libx264, `.webm` libvpx-vp9, `.mkv` ffv1,
  `.gif`). Encoded bytes are never compared (muxer metadata): tests use ffprobe facts and decoded pixels (lossless FFV1 for exact equality). Encoding goes to a
  temp sibling renamed on success; failed runs leave no file.
- **Shared box.** Load average is often 5-25: build and test with `nice`, one heavy build at a time (`make` of the Skia-linking binaries is the heavy part).
- **SDK pin sync.** `cpp/dslvm.hpp` must match `sdk.lock` (cpp-dsl-sdk commit + sha256); `test.sh` fails on drift; refresh with `scripts/sync-sdk.sh` from the sibling
  checkout `../cpp-dsl-sdk` (the integration owner refreshes it after the matching runtime is verified). Compiler, SDK and host move together: the compiler must match the SDK's bytecode version.
- **Stale local build.** A `build/lmvideohost` older than the schema fails with `schema: a signature needs params (array) and returns (string)` (schema 2 vs old host);
  run `nice make`. `test.sh` builds into a temp dir, so it is unaffected.
- **lmscript has no `int()`.** Examples read numeric ARGV with `num(...)`; DSL float geometry from int sizes needs `float(...)`; `${3.0}` is the int `3`.
- **`${}` bakes paths into the bytecode**, so bytecode size differs between machines/runs (the 2220 vs 3072 bytes template records).
- **Host-owned checks.** The SDK adds no type/identity/limit checks (R33/R34): the argument/result checks, the registry self-check, size limits (8192 px per side, 100000
  frames) and the lifecycle (begin/frame/end) are lmvideo's own. Do not delete them when the SDK is refreshed.
- **Name collision.** `lmvideo` is also the old name of lmcreator; do not "fix" docs by pointing there.
