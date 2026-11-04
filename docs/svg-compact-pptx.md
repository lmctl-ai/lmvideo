# Reuse one compact SVG in PowerPoint

[`examples/svg-compact-pptx.lms`](../examples/svg-compact-pptx.lms) keeps the artwork in one `_xml_ profile="svg-compact@1"` block. lmscript validates the XML and the `svg-compact@1` vocabulary before any host call.

The script sends `svg_data.toString()` to lmvideo's `svg(string)` function. lmvideo's dry callback parses and validates the SVG without rendering. Real mode draws the same document into a 160×100 PNG. The script then sends that PNG path to the pinned lmoffice PowerPoint host, which embeds it as a picture on a slide and writes a `.pptx`.

The SVG keeps one explicit default namespace on the root and a `viewBox`; width and height are inferred from that viewBox. Unitless coordinates and explicit fills keep the source compact and predictable. No stylesheet, external resource, or custom XML-to-shape translator is needed.

The PowerPoint copy is a raster picture, not editable vector artwork. It uses the PNG's 160×100 pixels and is enlarged to 480×300 on the slide, so edges can soften when zoomed or printed. To keep the picture sharper, increase the video canvas dimensions and matching SVG drawing size together.

Run with lmscript 0.2.460 or the current private latest compiler. The example pins lmvideo v0.1.72 and lmoffice v0.1.34. `--dry-run --plan-format json` validates both reached host programs but creates neither output file. Real mode writes `build/svg-compact.png` and `build/svg-compact.pptx`.
