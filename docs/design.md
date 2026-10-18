# lmvideo design note (Phase 0)

Status: EXPERIMENTAL. Operator brief R23 (lmctl-mono `lmscript/durable-memory/requirements/dsl-bytecode.md`): bindings for ffmpeg and
Skia, exposed to the lmscript `_dsl_` framework as host objects. The DSL model is R12/R13: the **host program owns the process,
the objects and the frame loop**; the DSL is a typed chunk (a callback) that runs inside a context the host hands it.

## 1. What Skija teaches (checked against HumbleUI/Skija, Apache-2.0)

Skija is Java over JNI over a prebuilt Skia. Its layout (`shared/java/*.java`, `platform/cc/*.cc`) is the same shape every
binding of Skia ends up with:

- **One thin native class per Skia type.** Each Java class holds a `long _ptr` to a heap C++ object plus a native finalizer
  (`_nGetFinalizer` returns the address of `deleteX`); every method is a static native `_nFoo(ptr, ...)` that calls the C++
  method. No logic lives in the glue. Objects Skia reference-counts (`sk_sp<>`: Image, Shader, Typeface, Surface...) are
  released with `unref`, plain value types (Paint, Path, Matrix, Rect) are `new`/`delete`d.
- **Value types are copied across the boundary** (Rect, Color4f, Matrix33 as float arrays/primitive args); only identity
  objects are handles.
- **Factories instead of constructors for managed types** (`Surface.makeRaster`, `Image.makeFromEncoded`, `Typeface.makeFromData`).
- **The API surface that matters for 2D image/video generation** (everything else is GPU, Vulkan/GL backends, Paragraph,
  Skottie, runtime effects, Picture/Document recording, Region, Codec animation):
  `Surface`, `Canvas` (draw/clip/save/transform), `Paint` (color, style, stroke, shader, mask/path/color filter, blend), `Path`
  and `PathBuilder` (+ `PathEffect` dash/corner, `PathMeasure`, `PathOp`), `Image` (+ encode PNG/JPEG/WEBP), `Font`/`Typeface`/
  `FontMgr`/`TextBlob`, `Shader` (gradients, image, noise), `Matrix33`, `Color`, `Rect`/`RRect`, `ImageFilter` (blur, drop shadow),
  `MaskFilter`, `ColorFilter`, `SVGDOM` (`shared/java/svg`), `Data`.

## 2. Skia acquisition: DECIDED, prebuilt JetBrains skia-pack

- Skija consumes the JetBrains `skia-pack` releases (`script/build.py --skia-release m143-da51f0d60e-5`); we use the same
  source, release **m144-e2e6623374-4**, asset `Skia-m144-e2e6623374-4-linux-Release-x64.zip`.
- URL: `https://github.com/JetBrains/skia-pack/releases/download/m144-e2e6623374-4/Skia-m144-e2e6623374-4-linux-Release-x64.zip`
  (the Debug zip is 632 MB and is NOT used). **Size: 49,703,089 bytes zipped, 179 MB unpacked**, under the 500 MB limit.
  sha256 pinned in `scripts/skia.lock`; `scripts/fetch-skia.sh` downloads and unpacks into the gitignored `third_party/skia`.
- It contains headers (`include/`, `modules/`) and **static libraries** (`libskia.a`, `libsvg.a`, `libskshaper.a`,
  `libskparagraph.a`, `libskottie.a`, `libskunicode_*`, `libpng/jpeg/webp/freetype2/harfbuzz/zlib/expat/icu`). Verified: a
  raster-surface-to-PNG probe links and runs with both g++ 11 and clang++ 14 (headers carry clang-only attributes that g++ warns
  about; we include Skia as a system header path). Static linking means the host binary has no runtime dependency on Skia.
- Not chosen: building Skia from source (gn + ninja + depot_tools, hours on this shared box, no benefit).
- Fontconfig is NOT installed; we never use a system font manager (see Fonts).

## 3. ffmpeg acquisition: DECIDED, drive the ffmpeg CLI through a pipe (v1)

- Installed: ffmpeg 4.4.2 + ffprobe at /usr/bin with `libx264`, `libvpx`, `libvpx-vp9`, `mpeg4`, `ffv1`, `png`. **No libav\* headers**,
  and no `-dev` packages may be installed.
- v1 spawns `ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r FPS -i - ... out.mp4` and writes each frame's RGBA bytes
  (`Surface::rgba()`) to its stdin; `ffprobe` reports frame count and duration for verification. This is the documented pattern,
  needs nothing new on the box and keeps encoder licensing (GPL x264) in a separate process.
- Rejected for v1: downloading a prebuilt shared FFmpeg with headers (BtbN/John Van Sickle style builds are 100+ MB, GPL or
  nonfree option mixes, and an ABI we would have to pin) and linking libav directly. The `Encoder` class is the seam: a
  libav-backed implementation can replace the pipe later without changing the DSL schema.
- Determinism: encoded bytes are not compared (the mp4 muxer writes a version string and timestamps). Tests assert ffprobe
  frame count, duration, size, codec, and decode specific frames back to RGBA and compare probe pixels with a tolerance.

## 4. Drawing format: DECIDED, Skia's own SVG DOM (`SkSVGDOM`) for the SVG subset

- `libsvg.a` is already in the prebuilt pack, so a general SVG-like drawing format costs one wrapper (`Svg::parse`,
  `Canvas::draw_svg`), shares the exact same rasterizer as the programmatic API, and is deterministic with our pinned font.
- Reuse of the org's Rust `resvg`/`tiny-skia` was considered: higher SVG fidelity, but it needs a Rust build/FFI hop, is a
  different rasterizer (pixel output differs from the Skia canvas, which would make mixed scenes inconsistent) and adds a
  dependency to every host. Recommendation: keep `Svg` behind one class; if a real document needs fidelity SkSVG lacks (full
  CSS, complex filters), add a resvg-backed path that rasterizes to an `Image` and draws that.
- Fonts: the DejaVu Sans file (`assets/fonts`, Bitstream Vera licence, redistributable with the notice) is **embedded in the
  binary** and served through Skia's FreeType custom font manager, so `<text>` and `Canvas::draw_text` never depend on system
  fonts and are identical across machines (hinting none, no subpixel positioning).

## 5. Layering

    Skia (static, prebuilt)  <-  cpp/lmvideo.{hpp,cpp}  (Skia-free public header, namespace lmv; Surface Canvas Paint Path Font Image Svg Encoder)
                                        ^                          ^
                              CLI `lmvideo` (render/demo)    DSL host `lmvideohost` (cpp/host.cpp + schema/lmvideo.dsl.json, dslvm.hpp)

The library is useful alone (CLI, tests). The DSL host is a second thin layer that implements the schema against the library.
Skia types never leave `lmvideo.cpp`: the public header uses value classes holding a shared implementation pointer.

## 6. How objects appear in a DSL schema (`dsl.schema` 1)

The schema format has methods only (no fields, no constructors, R11), so:

- **Object types** (opaque handles): `Canvas`, `Paint`, `Path`, `Font`, `Image`, `Svg`, `Surface`; video side: `Frame`, `Video`.
- **Constructors are host functions**: `paint()`, `path()`, `font(size)`, `image_file(path)`, `svg(text)`, `surface(w, h)`.
- **Builders return self** (`paint().color("#ff8800").stroke().stroke_width(3.0)`), which the POC findings already observed as the
  recurring idiom (setter-returns-self); no optional arguments, no lists.
- **Colours are strings** (`"#rrggbb[aa]"`, a few names) because the DSL has no struct or colour type; the host parses and
  rejects bad values with a located run-time error. Numbers are `float` (coordinates) and `int` (sizes, counts).
- **Ownership**: the host keeps every object in an arena for the run; the chunk cannot free, store or return handles (R11/R12),
  exactly as in poc-numpy.
- **Image mode**: the context is `canvas: Canvas`, `out: Output`; the chunk runs once. **Video mode**: the host owns the frame
  loop and runs the chunk once per frame with `frame: Frame` (`index()`, `count()`, `t()` in 0..1, `time()` seconds, `width()`,
  `height()`) plus `canvas`; animation is arithmetic on `frame.t()`. This is the R12 callback model; the DSL needs no loop.
  Evaluation of this choice is in the README (Phase 4).

## 7. Licensing

Skija: Apache-2.0 (study only, no code copied). Skia: BSD-3-Clause, shipped as static libs from JetBrains skia-pack (which
bundles libpng, libjpeg-turbo, libwebp, freetype, harfbuzz, expat, zlib, ICU under their own permissive licences: keep
`third_party/skia`'s notices when distributing a binary). DejaVu Sans: Bitstream Vera licence (`assets/fonts/LICENSE-DejaVu.txt`).
ffmpeg is invoked as a separate process and never linked (the installed build is GPL via libx264). `dslvm.hpp` comes from the
org's `cpp-dsl-sdk`.
