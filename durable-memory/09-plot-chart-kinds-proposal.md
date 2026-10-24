# Slice 3 proposal: field, statistical, and time-series chart kinds

## Scope

Extend the retained `Plot` API with data objects for matrix and vector fields, additional categorical/statistical series, and explicit scale/inset controls. Rendering stays deterministic and uses the existing Skia canvas. Collections remain typed schema objects populated with `add` calls; no array-valued DSL parameters are introduced. The plot remains composable inside the explicit Panel layout managers from Slice 2.

## Exact schema addition

Add these methods to `Plot`:

```json
{
  "heatmap": [{"params":["string","int","int"],"returns":"Heatmap"}],
  "quiver": [{"params":["string"],"returns":"VectorField"}],
  "inset": [{"params":["float","float","float","float"],"returns":"Plot"}],
  "x_date": [{"params":["string"],"returns":"Plot"}],
  "y_symlog": [{"params":["float"],"returns":"Plot"}],
  "x_symlog": [{"params":["float"],"returns":"Plot"}]
}
```

Add these methods to `Series`:

```json
{
  "add_band": [{"params":["float","float","float"],"returns":"Series"}],
  "histogram": [{"params":["int"],"returns":"Series"}],
  "box": [{"params":["float","float","float","float","float"],"returns":"Series"}],
  "stack": [{"params":["string"],"returns":"Series"}],
  "group": [{"params":["string"],"returns":"Series"}],
  "donut": [{"params":["float"],"returns":"Series"}]
}
```

Add data types:

```json
{
  "Heatmap": {"methods": {
    "cell": [{"params":["int","int","float"],"returns":"Heatmap"}],
    "palette": [{"params":["string"],"returns":"Heatmap"}],
    "range": [{"params":["float","float"],"returns":"Heatmap"}],
    "colorbar": [{"params":["bool"],"returns":"Heatmap"}],
    "labels": [{"params":["bool"],"returns":"Heatmap"}],
    "contours": [{"params":["int","string","float"],"returns":"Heatmap"}],
    "clear": [{"params":[],"returns":"Heatmap"}],
    "draw": [{"params":[],"returns":"Heatmap"}]
  }},
  "VectorField": {"methods": {
    "add": [{"params":["float","float","float","float"],"returns":"VectorField"}],
    "color": [{"params":["string"],"returns":"VectorField"}],
    "scale": [{"params":["float"],"returns":"VectorField"}],
    "streamlines": [{"params":["bool"],"returns":"VectorField"}],
    "clear": [{"params":[],"returns":"VectorField"}],
    "draw": [{"params":[],"returns":"VectorField"}]
  }}
}
```

`Plot.heatmap(name, rows, columns)` creates a row-major fixed-size grid, populated by `Heatmap.cell(row,column,value)`. Palettes are `viridis`, `magma`, `coolwarm`, and `gray`; optional cell labels use contrast-aware foreground colors. Optional contour levels overlay deterministic marching-squares lines. A colorbar is part of the plot's right-side margin. `Plot.quiver(name)` retains `(x,y,u,v)` vectors; arrows are normalized to a consistent visual length using the declared scale, and optional streamline mode uses a fixed deterministic grid integration. Both objects are redrawn on each video frame, so clearing/refilling their data supports animation.

The supported additional series kinds are `histogram`, `box`, `grouped_bar`, `stacked_bar`, `pie`, and `donut`. `histogram(bins)` configures sample binning; `box(min,q1,median,q3,max)` adds one summary at the series x coordinate; grouping/stacking are named explicitly. Pie/donut values are positive and rendered as deterministic sectors. `add_band(x,low,high)` defines an error band around an existing line series. Symlog uses a positive linear threshold and the continuous transform `sign(v)*log10(1+abs(v)/threshold)`, with original-value tick labels. Date axes use numeric Unix seconds and a declared format/time zone; nice ticks use UTC calendar intervals and fixed locale-independent labels. Insets are child plots with normalized rectangles inside the parent plot area; they have independent axes and use no inferred placement.

All dimensions, ranges, data, bins, thresholds, and scales are validated as finite and positive where required. Drawing order is insertion order: heatmaps then contours, vector fields after rasters, and series in their existing order. Pixel output must not depend on host locale or unordered-container iteration.

## Acceptance examples

Port ROC AUC, precision-recall, confusion matrix, Poisson contours, cavity quiver, symlog outlier, and residual scatter from `lmplot/examples`, preserving their deterministic input data and computed metrics. Include an evolving standing-wave heatmap/quiver MP4 and a README comparison image with lmplot and lmvideo panels. Tests cover palette endpoints, matrix-cell/colorbar layout, contrast text, contour interpolation, vector arrow orientation, symlog ticks, date tick formatting, histogram/bar grouping, pie sectors, band bounds, and inset clipping.

## Implementation record

Implemented in `cpp/plot.hpp`, `cpp/plot.cpp`, and `cpp/host.cpp`; the registry and `schema/lmvideo.dsl.json` expose the methods above. `Heatmap` owns a row-major fixed grid and supports viridis, magma, coolwarm, and gray interpolation, explicit value bounds, contrast-aware numeric labels, a colorbar, and marching-squares contour segments. Row zero is drawn at the top. A `VectorField` retains `(x,y,u,v)` samples; arrows are capped to 22 px, and optional streamlines use deterministic nearest-sample Euler integration with at most 48 steps from each sample seed.

Series kinds now include `histogram`, `grouped_bar`, `stacked_bar`, `box`, `pie`, and `donut` in addition to the original kinds. Histogram observations use `Series.add(value, weight)` with the value in `x`; the `y` argument is ignored and `.histogram(bins)` sets the bin count. Box summaries use `.box(min,q1,median,q3,max)`; each call occupies the next evenly spaced category. Pie and donut values use `Series.add(category_index, value)` and deterministic palette rotation; slice labels are not rendered. `.group(name)` and `.stack(name)` set explicit category group and stack keys. `add_band(x,low,high)` adds a filled polygon to line/area series.

`x_symlog(threshold)` and `y_symlog(threshold)` use `sign(v)*log10(1+abs(v)/threshold)` and render original-value tick labels. `x_date(format)` accepts `%Y-%m` or `%Y-%m-%d`, maps numeric Unix seconds to UTC, and chooses fixed day/month tick intervals. `inset(x,y,width,height)` creates an independently scaled plot in a normalized rectangle relative to the parent's outer plot bounds; its bounds are recalculated when the parent is drawn, including when Panel layout reflows it.

Examples include the seven lmplot ports plus `chart-kinds-workbench.lms`, which exercises histogram, box, grouped/stacked bars, donut, date ticks, error bands, and inset composition. `standing-wave-field.lms` redraws a 33×33 heatmap and its gradient vectors over 60 video frames. Golden tests cover palette endpoints/colorbar direction and a symlog marker position; `test.sh` compiles and renders all new examples and checks the animation's frame count.

Limitations: date ticks are UTC and the supported formats are numeric; vector streamlines are a small deterministic nearest-neighbor integrator rather than a high-order field solver; pie/donut slices currently have no per-slice text labels. Insets use the parent rectangle rather than its inner data-area rectangle.
