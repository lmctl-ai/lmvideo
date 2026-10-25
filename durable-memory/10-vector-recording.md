# Slice 4: vector outputs and picture recording

## Pinned Skia pack inventory

The pinned JetBrains pack is `m144-e2e6623374-4`. It has `libsvg.a` and `libskia.a`; `SkSVGCanvas::Make`, `SkPDF::MakeDocument`, and `SkPicture::serialize` / `SkPicture::MakeFromData` resolve from the included libraries (SVG canvas and PDF symbols are in `libskia.a`). No separate `libpdf.a` is present. `SkXPSDocument.h` is present only as a Windows-guarded declaration (`SK_BUILD_FOR_WIN`) requiring `IXpsOMObjectFactory`; there are no XPS symbols in the Linux x64 archive, and lmvideo currently builds/tests Linux and macOS. XPS is not an available output target for these builds.

`SkSVGCanvas` can convert text to path outlines; the recorder export enables this to avoid font substitution. Skia/PDF embeds/subsets the referenced typeface. SKP serialization is available in the core library; the implementation supplies PNG image serialization callbacks because pictures otherwise omit recorded images by default.

## Typed API

```json
{
  "functions": {
    "document": [{"params":["int","int"],"returns":"Document"}],
    "pdf_document": [{"params":["float","float"],"returns":"PdfDocument"}],
    "picture_file": [{"params":["string"],"returns":"Picture"}]
  },
  "Canvas": {"picture": [{"params":["Picture","float","float","float","float"],"returns":"void"}]},
  "Document": {"canvas": [{"params":[],"returns":"Canvas"}], "finish": [{"params":[],"returns":"Picture"}]},
  "Picture": {
    "width": [{"params":[],"returns":"int"}], "height": [{"params":[],"returns":"int"}],
    "draw": [{"params":["Canvas","float","float","float","float"],"returns":"void"}],
    "png": [{"params":["string","int"],"returns":"void"}],
    "svg": [{"params":["string"],"returns":"void"}],
    "pdf": [{"params":["string"],"returns":"void"}],
    "skp": [{"params":["string"],"returns":"void"}]
  },
  "PdfDocument": {
    "add_page": [{"params":["Picture"],"returns":"PdfDocument"}],
    "pages": [{"params":[],"returns":"int"}], "write": [{"params":["string"],"returns":"void"}]
  },
  "Surface": {
    "png": [{"params":["string"],"returns":"void"}],
    "svg": [{"params":["string"],"returns":"void"}],
    "pdf": [{"params":["string"],"returns":"void"}]
  }
}
```

`document(width,height)` records one Canvas command stream. `Document.finish()` invalidates the recording canvas and returns an immutable `Picture`. The picture can write 1×–4× PNG, SVG, one-page PDF, or serialized SKP and can replay to another Canvas rectangle. SVG output outlines text. `pdf_document(width,height)` accepts same-size pictures in order and writes a multi-page PDF. `Surface.svg/pdf` exports the existing raster surface as an embedded raster image; use `Document` for vector chart/layout output. Raster scale factors are validated and output dimensions may not exceed 16384 pixels.

The C++ API is in `cpp/lmvideo.hpp` / `cpp/lmvideo.cpp`; the DSL registry and schema expose `Document`, `Picture`, and `PdfDocument`. Picture serialization installs a PNG encoder/decoder callback for recorded `SkImage`s. PDF metadata and generated output are deterministic in the pixel/vector tests. The direct-PNG test decodes the encoded PNG and compares every RGBA pixel against direct `SkPicture` playback; the SVG test loads the exported file through `svg_file()` and asserts its raster PNG is byte-identical to the direct picture PNG. `tests/vector_test.cpp` has 16 checks covering 1×/2×/3× sizes, SVG path text, PNG-backed SKP round trip, one-/two-page PDFs, and raster Surface SVG/PDF wrappers. `examples/vector-outputs.lms` writes the formats plus high-DPI PNGs and records a Panel dashboard as its second PDF page.

## Office image packaging

Microsoft's Office Drawing extension specification says SVG content is stored in an image part and referenced through the `svgBlip` extension; for compatibility, a PNG raster fallback is referenced by the normal DrawingML blip ([pictures](https://learn.microsoft.com/en-us/openspecs/office_standards/ms-odrawxml/7e1f1524-1569-4aa2-a6c9-aab2d855bd48), [svgBlip](https://learn.microsoft.com/en-us/openspecs/office_standards/ms-odrawxml/2451f45e-5d77-4661-86d1-0a017fced779)). `svgBlip` uses the `http://schemas.microsoft.com/office/drawing/2016/SVG/main` namespace and an embedded image relationship. LMOffice's DOCX/PPTX APIs still accept PNG/JPEG only. A scratch one-slide PPTX containing a PNG fallback and a related SVG image part passed ZIP integrity checks and `python-pptx` structural parsing; it was not rendered in Microsoft Office. Its backlog now records that image-part support must emit both the fallback and SVG extension.
