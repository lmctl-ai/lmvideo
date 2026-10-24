# Plot and Series schema proposal

## Purpose

Add a compact, retained chart API over the existing Skia `Canvas`. A plot draws into the same canvas as low-level shapes and is therefore usable on a still `Surface` or on the canvas slot rebound by each `Video.frame()`. A retained plot and its series can be drawn again after each frame begins. No arrays cross the DSL boundary: a `Series` owns ordered samples appended one at a time.

All coordinates are logical canvas pixels and all data values are finite binary64 values. Data points are stored in insertion order. `draw()` paints the chart when called; it does not clear the canvas or change canvas state outside its own save/restore scope. Plot defaults are linear axes, automatic ranges, visible grid, transparent background, and a top-right legend for nonempty series names. Empty series names are omitted from the legend.

## Exact schema addition

```json
{
  "Canvas": {
    "methods": {
      "plot": [{"params": ["float", "float", "float", "float"], "returns": "Plot"}]
    }
  },
  "Plot": {
    "methods": {
      "title": [{"params": ["string"], "returns": "Plot"}],
      "xlabel": [{"params": ["string"], "returns": "Plot"}],
      "ylabel": [{"params": ["string"], "returns": "Plot"}],
      "y2label": [{"params": ["string"], "returns": "Plot"}],
      "x_range": [{"params": ["float", "float"], "returns": "Plot"}],
      "y_range": [{"params": ["float", "float"], "returns": "Plot"}],
      "y2_range": [{"params": ["float", "float"], "returns": "Plot"}],
      "x_scale": [{"params": ["string"], "returns": "Plot"}],
      "y_scale": [{"params": ["string"], "returns": "Plot"}],
      "legend": [{"params": ["string"], "returns": "Plot"}],
      "grid": [{"params": ["bool"], "returns": "Plot"}],
      "background": [{"params": ["string"], "returns": "Plot"}],
      "series": [{"params": ["string", "string", "string"], "returns": "Series"}],
      "annotate": [{"params": ["float", "float", "string", "string"], "returns": "Plot"}],
      "vline": [{"params": ["float", "string", "float"], "returns": "Plot"}],
      "hline": [{"params": ["float", "string", "float"], "returns": "Plot"}],
      "draw": [{"params": [], "returns": "Plot"}]
    }
  },
  "Series": {
    "methods": {
      "add": [{"params": ["float", "float"], "returns": "Series"}],
      "add_error": [{"params": ["float", "float", "float", "float"], "returns": "Series"}],
      "clear": [{"params": [], "returns": "Series"}],
      "axis": [{"params": ["string"], "returns": "Series"}],
      "color": [{"params": ["string"], "returns": "Series"}],
      "width": [{"params": ["float"], "returns": "Series"}],
      "dash": [{"params": ["string"], "returns": "Series"}],
      "marker": [{"params": ["string"], "returns": "Series"}]
    }
  }
}
```

`Canvas.plot(x, y, width, height)` returns a plot attached to the receiver's live canvas slot. Width and height must be positive. Plot and series setters are fluent. `Plot.series(name, kind, color)` accepts `kind` `line`, `scatter`, `bar`, `step`, `area`, or `errorbar`; its returned `Series` is the sole typed collection API for point data. `Series.add_error(x, y, low, high)` records absolute lower and upper Y data coordinates for an error-bar sample; it may only be used by an `errorbar` series. `Series.axis` accepts `left` or `right`; the right axis is enabled by `y2_range` and uses the independent `y2label` and range.

`x_scale` and `y_scale` accept `linear` or `log`. Logarithmic axes require positive explicit bounds and ignore no invalid samples: a draw with nonpositive data on a logarithmic dimension fails with a DSL error. Automatic ranges include all finite samples assigned to that axis, with a stable nonzero padding for constant-valued data. Explicit ranges require finite increasing bounds. Automatic linear ticks use a 1, 2, 2.5, 5 × 10^n step selected for about five intervals, with matching grid lines and labels. Log ticks use decade majors and 2–9 minor ticks; labels are printed on decade majors only.

Legend positions are `top_right`, `top_left`, `bottom_right`, `bottom_left`, or `none`. Series kinds, scale names, axis names, legend positions, dash names (`solid`, `dashed`, `dotted`, `dash_dot`), marker names (`none`, `circle`, `square`, `triangle`, `plus`) and colors are validated. `vline`/`hline` widths and series widths are positive pixels. `annotate` places left-aligned DejaVu Sans text at the given data coordinate. The plot uses the bundled DejaVu Sans face and the same deterministic Skia text settings as the low-level API.

The initial implementation covers independent linear/log X and left Y axes, optional right Y axis, line/scatter/bar/step/area/error-bar series, text and reference-line annotations, range selection, grid, and a positioned legend. It intentionally leaves retained groups, node mutation, and additional chart families for a later slice.
