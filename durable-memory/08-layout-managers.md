# Slice 2: explicit layout managers

## Goal and boundaries

Compose plots, text, shapes, and images in predictable `Panel` containers on the current Skia `Canvas`. A root panel has an explicit canvas rectangle; nested panels use a manager selected in the script. Child placement is controlled by constraints on each returned `LayoutItem`. No manager infers a graph or diagram topology, and no layout decision is hidden from the author.

A panel or child can declare preferred, minimum, and maximum sizes. Panel padding and gap use logical canvas pixels. Alignment, fill, expansion, weights, grid spans, border regions, and absolute placement are explicit constraints. Text uses the embedded DejaVu Sans face. Images use an existing `Image` handle. A layout is redrawn on each current video frame like `Plot`; the widget tree and the plot data remain retained for the chunk.

## Exact schema addition

```json
{
  "Canvas": {
    "methods": {
      "panel": [{"params": ["float", "float", "float", "float"], "returns": "Panel"}]
    }
  },
  "Panel": {
    "methods": {
      "manager": [{"params": ["string"], "returns": "Panel"}],
      "padding": [{"params": ["float", "float", "float", "float"], "returns": "Panel"}],
      "gap": [{"params": ["float"], "returns": "Panel"}],
      "background": [{"params": ["string"], "returns": "Panel"}],
      "edge": [{"params": ["string", "float"], "returns": "Panel"}],
      "align": [{"params": ["string", "string"], "returns": "Panel"}],
      "min_size": [{"params": ["float", "float"], "returns": "Panel"}],
      "max_size": [{"params": ["float", "float"], "returns": "Panel"}],
      "columns": [{"params": ["int"], "returns": "Panel"}],
      "row_weight": [{"params": ["int", "float"], "returns": "Panel"}],
      "column_weight": [{"params": ["int", "float"], "returns": "Panel"}],
      "add_panel": [{"params": [], "returns": "LayoutItem"}],
      "add_plot": [{"params": [], "returns": "LayoutItem"}],
      "add_text": [{"params": ["string"], "returns": "LayoutItem"}],
      "add_shape": [{"params": ["string"], "returns": "LayoutItem"}],
      "add_image": [{"params": ["Image"], "returns": "LayoutItem"}],
      "add_glue": [{"params": [], "returns": "LayoutItem"}],
      "draw": [{"params": [], "returns": "Panel"}]
    }
  },
  "LayoutItem": {
    "methods": {
      "panel": [{"params": [], "returns": "Panel"}],
      "plot": [{"params": [], "returns": "Plot"}],
      "text": [{"params": [], "returns": "Text"}],
      "shape": [{"params": [], "returns": "Shape"}],
      "image": [{"params": [], "returns": "ImageView"}],
      "pack": [{"params": ["string", "string", "bool"], "returns": "LayoutItem"}],
      "fill": [{"params": ["string"], "returns": "LayoutItem"}],
      "grid": [{"params": ["int", "int", "int", "int"], "returns": "LayoutItem"}],
      "border": [{"params": ["string"], "returns": "LayoutItem"}],
      "place": [{"params": ["float", "float", "float", "float"], "returns": "LayoutItem"}],
      "preferred_size": [{"params": ["float", "float"], "returns": "LayoutItem"}],
      "min_size": [{"params": ["float", "float"], "returns": "LayoutItem"}],
      "max_size": [{"params": ["float", "float"], "returns": "LayoutItem"}],
      "weight": [{"params": ["float"], "returns": "LayoutItem"}],
      "sticky": [{"params": ["string"], "returns": "LayoutItem"}],
      "align": [{"params": ["string", "string"], "returns": "LayoutItem"}]
    }
  },
  "Text": {
    "methods": {
      "color": [{"params": ["string"], "returns": "Text"}],
      "size": [{"params": ["float"], "returns": "Text"}],
      "bold": [{"params": ["bool"], "returns": "Text"}],
      "align": [{"params": ["string"], "returns": "Text"}]
    }
  },
  "Shape": {
    "methods": {
      "fill": [{"params": ["string"], "returns": "Shape"}],
      "stroke": [{"params": ["string"], "returns": "Shape"}],
      "stroke_width": [{"params": ["float"], "returns": "Shape"}],
      "radius": [{"params": ["float"], "returns": "Shape"}]
    }
  },
  "ImageView": {
    "methods": {
      "fit": [{"params": ["string"], "returns": "ImageView"}]
    }
  }
}
```

`Canvas.panel(x, y, width, height)` creates the positioned root. Nested panels are added as layout items; `LayoutItem.panel()`, `.plot()`, `.text()`, `.shape()`, and `.image()` return the selected child object for styling. `Panel.manager` accepts only `pack`, `grid`, `place`, `border`, `flow_h`, `flow_v`, `box_h`, or `box_v`. A panel defaults to `pack`, but examples set the manager explicitly. Call `Panel.draw()` after configuring and filling the tree.

For `pack`, `LayoutItem.pack(side, fill, expand)` accepts side `top`, `bottom`, `left`, or `right`; fill `none`, `x`, `y`, or `both`; and distributes remaining space among expanded items in insertion order. `LayoutItem.fill` selects cross-axis stretching for flow and box children. `grid` requires `Panel.columns(n)` and uses zero-based row/column, positive row/column spans, optional row/column weights, and `sticky` from the `n`, `s`, `e`, `w` combinations. `border` places at most one child in each `north`, `south`, `east`, `west`, and `center` region. `place` uses coordinates relative to the padded inner panel. `flow_h`/`flow_v` place children in order at preferred sizes with gaps and do not wrap. `box_h`/`box_v` use preferred sizes, gap, item weights, and glue items to distribute remaining space along one axis.

Panel and child min/max sizes must be finite and nonnegative; when both are set, minimum cannot exceed maximum. Padding and gap are finite nonnegative pixels. Grid dimensions/spans are positive, indexes are nonnegative, weights are finite nonnegative values, and item place rectangles have positive dimensions. Horizontal alignment is `left`, `center`, or `right`; vertical alignment is `top`, `center`, or `bottom`. `Shape` kinds are `rect`, `round_rect`, `ellipse`, and `line`; image fit is `contain`, `cover`, or `stretch`. An absent preferred size comes from the child: measured text, source image dimensions, a 320×200 plot, or the available cell for a shape/glue.

## Examples and tests

`dashboard-grid.lms` is a 2×2 figure using `grid`. `layout-workbench.lms` combines `border`, `place`, `box_h`, `flow_v`, image, shape, text, and plot children. The existing `quarterly-revenue`, `sensor-calibration`, and `forecast-area` examples now compose their headings, charts, side notes, and footers with box, border, pack, and flow managers. Golden probes cover pack expansion, weighted grid cells/spans, border regions, explicit place coordinates, flow order, and horizontal/vertical box spacing. `test.sh` renders the examples and checks schema/compiler signatures.

## Implementation record

Slice 2 is implemented in `cpp/layout.hpp` and `cpp/layout.cpp`, with callbacks registered in `cpp/host.cpp`. A `Panel` stores the stable `CanvasSlot*`; `Panel.draw()` recomputes child rectangles for the current frame and then draws each child in order. `Plot` bounds are reassigned before each child draw. Text, shape, image and nested panel handles live in the host's per-run arena. Parent panels clip their children to the panel rectangle. `LayoutItem.fill` sets cross-axis fill for flow and box children; pack items can set fill and expansion together through `LayoutItem.pack`.

The registry exposes all eight explicit managers: `pack`, `grid`, `place`, `border`, `flow_h`, `flow_v`, `box_h`, and `box_v`. Grid supports weighted rows/columns and spans; border rejects duplicate regions; place rectangles must fit the padded content box; flow does not wrap; box distributes leftover space using item weights and glue. No manager inspects graph structure or infers a node arrangement.

The three Plot examples reflow existing examples: quarterly revenue uses a box with a title and chart, sensor calibration uses border regions for heading/chart/footer, and forecast area uses a packed heading/body/footer with a box and flow sidebar. `dashboard-grid.lms` exercises weighted grid cells. `layout-workbench.lms` combines border, box, flow, place, text, shapes, a generated image, and a Plot. `tests/layout_test.cpp` checks pack expansion, weighted grid and spans, border regions, place, flow order, and horizontal/vertical box glue.
