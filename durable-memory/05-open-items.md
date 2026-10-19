# Open items

- **libav in-process backend** for `Encoder` (no process, in-memory encode): needs prebuilt shared FFmpeg with headers (or an installed `-dev` package); nothing attempted.
- **GPU backends** (Skia Ganesh/Graphite, Vulkan/GL) are not used; raster only.
- **Platforms:** Linux x86-64 only (the pinned zip). Windows/macOS/arm64 skia-pack archives exist but are not wired in `fetch-skia.sh`/Makefile.
- **Not exposed to the DSL / not in the library:** `Matrix`, `Shader` objects (only a two-colour linear gradient exists), `ImageFilter`, `Paragraph`, `Skottie`, `PathEffect`
  beyond dash, text layout beyond one line, audio, animated GIF/WebP reading.
- **`Font::from_file`** exists in the library but is NOT exposed to the DSL: reading a path named by the script is a security decision for the lmscript team.
- **SVG fidelity:** SkSVG supports a practical subset (no scripting, limited filters/CSS); a resvg-backed path rasterising to an `Image` is the fallback if needed.
- **Rust host (R21):** not attempted; a Rust port would use `skia-safe` and share the schema and the SDK conformance vectors.
- **State across frames** (physics/particles) is only expressible with DSL locals inside the frame loop or host collections; a persistent host `State` object is untested.
- **Release promotion:** none done; released lmscript 0.2.328 emits v3 and is refused. Re-pin `sdk.lock` and rerun `test.sh` when the matching release exists.
