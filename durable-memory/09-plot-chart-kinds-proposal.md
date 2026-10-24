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
    "draw": [{"params":[],"returns":"Heatmap"}]
  }},
  "VectorField": {"methods": {
    "add": [{"params":["float","float","float","float"],"returns":"VectorField"}],
    "color": [{"params":["string"],"returns":"VectorField"}],
    "scale": [{"params":["float"],"returns":"VectorField"}],
    "streamlines": [{"params":["bool"],"returns":"VectorField"}],
    "draw": [{"params":[],"returns":"VectorField"}]
  }}
}
```

`Plot.heatmap(name, rows, columns)` creates a row-major fixed-size grid, populated by `Heatmap.cell(row,column,value)`. Palettes are `viridis`, `magma`, `coolwarm`, and `gray`; optional cell labels use contrast-aware foreground colors. Optional contour levels overlay deterministic marching-squares lines. A colorbar is part of the plot's right-side margin. `Plot.quiver(name)` retains `(x,y,u,v)` vectors; arrows are normalized to a consistent visual length using the declared scale, and optional streamline mode uses a fixed deterministic grid integration. Both objects are redrawn on each video frame, so clearing/refilling their data supports animation.

The supported additional series kinds are `histogram`, `box`, `grouped_bar`, `stacked_bar`, `pie`, and `donut`. `histogram(bins)` configures sample binning; `box(min,q1,median,q3,max)` adds one summary at the series x coordinate; grouping/stacking are named explicitly. Pie/donut values are positive and rendered as deterministic sectors. `add_band(x,low,high)` defines an error band around an existing line series. Symlog uses a positive linear threshold and the continuous transform `sign(v)*log10(1+abs(v)/threshold)`, with original-value tick labels. Date axes use numeric Unix seconds and a declared format/time zone; nice ticks use UTC calendar intervals and fixed locale-independent labels. Insets are child plots with normalized rectangles inside the parent plot area; they have independent axes and use no inferred placement.

All dimensions, ranges, data, bins, thresholds, and scales are validated as finite and positive where required. Drawing order is insertion order: heatmaps then contours, vector fields after rasters, and series in their existing order. Pixel output must not depend on host locale or unordered-container iteration.

## Acceptance examples

Port ROC AUC, precision-recall, confusion matrix, Poisson contours, cavity quiver, symlog outlier, and residual scatter from `lmplot/examples`, preserving their deterministic input data and computed metrics. Include an evolving standing-wave heatmap/quiver MP4 and a README comparison image with lmplot and lmvideo panels. Tests cover palette endpoints, matrix-cell/colorbar layout, contrast text, contour interpolation, vector arrow orientation, symlog ticks, date tick formatting, histogram/bar grouping, pie sectors, band bounds, and inset clipping.
