# 06 - FFmpeg in process: feasibility

Question (2026-10-24): can lmvideo link FFmpeg's libraries so scripts drive encoding (and later decoding and filtering) directly, instead of piping RGBA
frames to an external `ffmpeg` command?

**Answer: yes, and it is cheap. A static, LGPL libav* build with libvpx and OpenH264 encodes everything lmvideo needs. The open decisions are licensing
policy (LGPL relinking, H.264 patents), not engineering.** Today `cpp/encoder.cpp` spawns `ffmpeg` (see 02-decisions.md); nothing here is implemented yet.

## Spike (reproducible)

`tools/ffmpeg-spike/run.sh` builds, as static libraries under `$SPIKE_DIR` (default `./ffspike`): nasm 2.16.03, libvpx v1.15.0, OpenH264 v2.6.0 and FFmpeg
n7.1.5 configured with `--disable-programs --disable-autodetect --disable-everything --enable-zlib` plus an explicit codec/muxer/filter list, then links
`enc.c` (about 60 lines of libav API: RGBA -> `sws_scale` -> encoder -> muxer) and runs it. Measured on 4 x86-64 cores, Ubuntu 22.04, gcc 11:

| item | result |
| --- | --- |
| build time, cold | nasm 7 s, libvpx 15 s, OpenH264 14 s, FFmpeg configure 8 s + make 38-47 s: **about 2 minutes** |
| static archives | libavcodec 6.6 MB, libvpx 3.4 MB, OpenH264 2.2 MB, libavformat 1.4 MB, libavutil 1.4 MB, libswscale 0.9 MB, libavfilter 0.6 MB, libswresample 0.2 MB (about 17 MB) |
| spike executable | 10.6 MB (glibc dynamic only; lmvideo today is 27.6 MB static, so expect roughly 35 MB) |
| FFmpeg license reported by configure | **LGPL version 2.1 or later** (no `--enable-gpl`, no x264/x265) |
| encode, 120 frames of 320x180 RGBA | VP9/WebM 225 KB, H.264/MP4 329 KB (OpenH264 at 900 kb/s), MPEG-4/MP4 175 KB, APNG 2.7 MB (30 frames); all four muxed and probed by an independent `ffprobe` (codec, size, frame count correct); about 1 s for all four |

What was **not** exercised: decoding, `libavfilter` graphs, audio, GIF palette handling (the GIF encoder needs PAL8 input, so a `palettegen`-style step), macOS and
arm64 builds (the same configure line is expected to work; `--enable-videotoolbox` gives a hardware H.264 encoder on macOS).

Pitfalls found while doing it: encoder names use a hyphen (`libvpx-vp9`) while configure names use an underscore (`libvpx_vp9`); the PNG/APNG encoders need
`--enable-zlib` (they silently disappear under `--disable-autodetect`); the x86 asm needs `nasm` on `PATH` at FFmpeg configure time; `AVCodec::pix_fmts` is
deprecated in 7.x (use `avcodec_get_supported_config`).

## Codec policy (the real decision)

| codec | library | license | notes |
| --- | --- | --- | --- |
| H.264 in MP4 | OpenH264 (Cisco) | BSD-2-Clause | Baseline profile, no B-frames: larger files than x264. H.264 is patent-encumbered: Cisco covers only the binaries it distributes. Building it ourselves puts the patent question on the distributor. |
| H.264, best quality | x264 | GPL-2.0-or-later | would make the combined binary GPL; rejected for lmvideo (MIT). Still available through the external `ffmpeg` backend. |
| VP9 / VP8 in WebM | libvpx | BSD-3-Clause | patent grant from Google; plays in browsers, VLC, ffmpeg; MP4 players on Apple devices historically do not. |
| MPEG-4 part 2 in MP4 | libavcodec native | LGPL | plays everywhere, poor compression. |
| APNG / PNG / MJPEG | libavcodec native (+zlib) | LGPL / zlib | lossless stills and animation. |
| AV1 | libaom or SVT-AV1 (BSD-2 + patent grant) | permissive | not spiked; slow encoders, large builds. |
| AAC audio | libavcodec native `aac` encoder | LGPL | enough for narration/music mux; Opus needs libopus (BSD-3). |

Linking FFmpeg statically under the LGPL requires that a user can relink the application against a modified FFmpeg: ship the application object files and the
link line with each release, exactly as lmcad does for OCCT (see `../lmcad/durable-memory/13-licensing.md`), plus the LGPL text and FFmpeg's `COPYING.LGPLv2.1`.
Add the exact notices of libvpx and OpenH264 to the release licenses (`scripts/stage-release-notices.sh` is the place).

## Recommended design

1. **Backends, not a replacement.** Keep the external-`ffmpeg` encoder (any codec the user's build has, including x264) and add a built-in libav backend selected
   per `Video` (`video.backend("builtin" | "ffmpeg")`, default builtin when compiled in, falling back to the CLI when `ffmpeg` is the only option for the requested codec).
   Both sit behind the `lmv::Encoder` interface in `cpp/encoder.hpp`, so the Skia/DSL side does not change.
2. **Phase 1: encode.** `Video.codec("h264" | "vp9" | "mpeg4" | "apng" | "gif")` (container from the output extension), `crf`/bitrate, `faststart` for MP4,
   BT.709 colour tagging so players do not shift colours, single-threaded deterministic mode for tests.
3. **Phase 2: decode and composite.** `movie_file(path)` returning an object with `frame_at(t) -> Image` (demux, decode, `sws_scale` to RGBA), so a script can draw Skia
   overlays on top of footage, make transitions and extract frames. Needs the native decoders already enabled in the spike.
4. **Phase 3: scripting FFmpeg proper.** Expose a small, typed subset of `libavfilter` (`scale`, `fade`, `xfade`, `overlay`, `concat`, `fps`, `trim`) as DSL objects or a
   filter-graph string, audio mux (`Video.audio(path)`), and `probe(path)` (replacing `ffprobe`). Do not expose arbitrary graph strings before there is a validation story:
   host policy (limits, file access) must stay in the host, as for every other call.
5. **Build.** One script per platform builds nasm, libvpx, OpenH264 and FFmpeg into `third_party/ffmpeg/` (cached in CI by script hash); CI cost is about 2-3 minutes
   uncached per platform. Windows is out of scope until the Skia pack and toolchain story exists.

## Risks

- libav API churn between major versions (7.x deprecations already visible); pin FFmpeg by tag and hash in a lock file like `scripts/skia.lock`.
- Binary size (+8 to 10 MB) and a second large third-party tree to keep notices for.
- Encoder determinism: threaded encoders (libvpx, OpenH264) are not bit-identical run to run; tests should compare decoded frames with a tolerance or force one thread.
- H.264 legal posture must be decided by the project owner before OpenH264 ships in release binaries; VP9 + MPEG-4 + APNG can ship without that question.
