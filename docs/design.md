# lmvideo design

lmvideo combines the lmscript host contract with Skia drawing and media output. A script configures a Canvas, Image, Plot, Panel, or Scene3D, then draws still images or video frames through the same author-facing API.

## Drawing and output

Canvas primitives, retained plots, explicit layout managers, diagrams, images, SVG/PDF documents, and CPU-rendered 3D scenes compose on the same output surface. A script selects the image or video dimensions and output path; the host owns the drawing surface and output lifecycle.

## Dependencies and notices

Skia provides raster and vector drawing. FFmpeg is called from `PATH` for video encoding. DejaVu Sans is bundled for consistent text metrics. The exact third-party license texts and notices are included in this public tree and with binary releases.

## lmscript programs

The schema exposes opaque objects such as `Canvas`, `Paint`, `Path`, `Image`, `Svg`, `Surface`, `Video`, `Plot`, `Panel`, and `Scene3D`. Constructors and collections are host functions and methods; collection iteration uses `len()` and `at()`.

The video lifecycle is explicit: configure `Video`, call `begin()`, call `frame()` once per loop iteration, draw, then call `end()`. SVG parsing, plotting, and diagram rendering use the same Canvas and Image output model.

## Reproducibility

The public-content workflow checks README links, image files, raw autorun URLs, and every `.lms` example against the released hosts referenced by those programs. Rendered image hashes and asset licenses are recorded in the example manifests.
