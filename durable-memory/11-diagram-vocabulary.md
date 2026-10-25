# Slice 4: lmdraw-compatible diagram vocabulary

## Goal and boundaries

Port the typed diagram API that lmdraw 0.2.6 introduced (`/home/mma/repos/dsl/lmdraw` at 0.2.6, `schema/lmdraw.dsl.json`, `docs/LLM_GUIDE.md` "Diagram building blocks") into lmvideo's Skia host so a diagram script ports between hosts by changing little. Shapes are labeled and carry named anchors; connectors route between raw points or `(shape, anchor)` endpoints; sequence helpers and grids cover the remaining lmdraw notation. Everything renders through the lmvideo value API (`lmv::Canvas`/`Paint`/`Path`, DejaVu Sans) and stays deterministic per platform like every other example.

This slice does not add auto-layout: all geometry stays explicit, mirroring lmdraw. The only "smart" behavior is the two documented resolution moments below.

## Exact schema addition

`Canvas` gains (new types in parentheses):

- `box(float cx, float cy, float w, float h, string label, string fill, string stroke, float radius, float label_size) -> Shape`
- `box(Shape at, string anchor, float w, float h, string label, string fill, string stroke, float radius, float label_size) -> Shape` (overloads `box` on the first parameter pair)
- `diamond(cx, cy, w, h, label, fill, stroke, label_size) -> Shape`
- `cylinder(cx, cy, w, h, label, fill, stroke, label_size) -> Shape` (height must exceed 20 px)
- `note(float x, float y, w, h, label, fill, stroke, label_size) -> Shape` (top-left origin, folded corner)
- `cluster(float x, float y, w, h, label, fill, stroke, radius, label_size) -> Shape` (top-left origin, bold header label in the stroke color)
- `connector(...)` in lmdraw's four overloads (point↔point, shape+anchor↔shape+anchor, and the two mixed forms), 14 parameters: `routing` (`straight|elbow|curve`), `start_marker`/`end_marker` (`none|arrow|dot|diamond`), `dash` (`none` or a stroke-dasharray list), `color`, `width`, `label`, `label_t` (0..1 along the drawn path), `label_dx`, `label_dy` -> `Group`
- `grid(float x, float y, float w, float h, int rows, int cols, float gap_x, float gap_y) -> Grid`
- `lifeline(float x, float y0, float y1, string color) -> Group`
- `message(float x1, float y1, float x2, float y2, string label, bool dashed, string color) -> Group`
- `activation(float x, float y0, float y1, string fill, string stroke) -> Group`
- `self_message(float x, float y, string label, string color) -> Group`
- `group() -> Group`
- `draw_diagram() -> void` — flush the retained diagram layer (lmvideo-specific; there is no `render_png` in a video host)

`Shape` (the same type the layout slice created) gains `label(string) -> Shape`, `set_attr(string, string) -> Shape`, `cx() -> float`, `cy() -> float`. `Group` gains `add(Group) -> Group` and `set_attr(string, string) -> Group`. `Grid` has `cell(int col, int row) -> Shape`. `Panel.add_shape` also accepts the diagram kinds (`box`, `diamond`, `cylinder`, `note`, `cluster`) so a labeled shape can be a layout child.

Schema 2 already allowed several signatures per method name; `cpp/schema.hpp` and the `Registrar` in `cpp/host.cpp` now keep the full overload set per name and the registry self-check compares parameter tuples per overload.

## Semantics and resolution order (authors must know)

1. Every `canvas.box/connector/grid/lifeline/...` call appends a retained item to the calling canvas slot's diagram layer (`CanvasSlot::diagram`, see `cpp/diagram.hpp`). Nothing draws until `canvas.draw_diagram()` runs; it redraws the whole layer in creation order on the current frame. Attributes updated between frames (`set_attr`) land on the next `draw_diagram()`, which is how all four animated examples work.
2. Immediate-mode drawing (`canvas.path`, `canvas.line`, `canvas.text`, …) paints at the call site. It lands **below** anything the next `draw_diagram()` draws and **above** anything already flushed. The state-machines and swimlane ports draw their explicit three-bend paths after the flush to reproduce lmdraw's creation-order z-stack.
3. `box(Shape at, "c", ...)` resolves the anchor **at creation time** (geometry is baked into the new shape).
4. `connector(a, "e", b, "w", ...)` stores the shape handles and resolves the anchors **at draw time**, each frame. A shape that is a `Panel` layout child receives its bounds from `panel.draw()` (the layout pass writes `shape.x/y/w/h` back, see `draw_shape` in `cpp/layout.cpp`); call `panel.draw()` before `canvas.draw_diagram()` so connector endpoints track the laid-out bounds. `cx()`/`cy()` on a layout child read the last layout pass's bounds.
5. `Group.set_attr(name, value)` forwards a presentation attribute to every child; `Group.add(g)` adopts a layer root into the group (cyclic groups are refused).
6. Acceptable `set_attr` names: shapes take `fill`, `stroke`, `stroke-width`, `opacity`, `visibility`; connectors also take `stroke-dasharray`, `stroke-dashoffset` (the draw-on animation: arm with a long `on off` pair, e.g. `"900 900"`, then lower the offset per frame), `marker-start`, `marker-end`, and `label` (lmvideo extension for per-frame label text); groups accept whatever their children accept. Unknown names are refused with a located run-time error (exit 3, `RUN_HOST`).

## Deltas vs lmdraw that authors must know

- **`draw_diagram()` replaces `render_png()`**: lmvideo owns frames; one flush per frame after `canvas.clear` is the port of one `render_png` call.
- **Fonts**: lmvideo embeds DejaVu Sans (lmdraw embeds Noto Sans + Noto Sans Math). Label metrics and glyph shapes differ slightly; wrapped labels re-wrap at lmvideo's measured widths.
- **Named colours**: lmvideo's colour parser accepts `#rgb/#rrggbb/#rrggbbaa` plus `black/white/red/green/blue/transparent`; lmdraw accepts more CSS names. Use hex colours for portability.
- **No SVG-node layer**: lmdraw's `set_attr` can restyle arbitrary retained SVG nodes (`transform`, `text-anchor`, gradients via `url(#id)`, `href`, …). lmvideo's diagram `set_attr` is scoped to the presentation list above; text positioning uses `Font.measure` (see `examples/org-chart-grid.lms`), and gradients are a `Paint` concern (`paint().gradient(...)`) outside the diagram layer.
- **No standalone polyline/path/marker nodes**: lmdraw examples that build custom arcs from raw SVG path data (`state-machines` door-lock arcs, `swimlane-process` retry loop) port to `path().move_to().cubic_to()/line_to()` plus a hand-built arrowhead triangle, drawn immediately around the flush.
- **`Scene.image/load_svg/load_font/measure_text`/`linear_gradient`** have no diagram-layer equivalent in this slice; `surface()`, `image_file`, `svg_file`, `paint().gradient`, and `Font.measure` cover the same ground outside the vocabulary.
- **Limits**: 10,000 diagram items per canvas slot, 64×64 grid maximum, labels at most 4096 bytes, dash lists of at most 32 positive lengths, wrapped labels at most 256 lines. Errors are located (`dsl line L:C`) and exit 3, matching the rest of the host; `out.fail` semantics are unchanged.

## Implementation map

- `cpp/diagram.hpp` / `cpp/diagram.cpp`: the retained item classes (`ShapeItem`, `ConnectorItem`, `LifelineItem`, `MessageItem`, `ActivationItem`, `SelfMessageItem`, `GroupItem`), the per-canvas `Layer`, anchor math, dash parsing, marker geometry, and the shared shape renderer (`render_shape`, also used by `cpp/layout.cpp` for diagram-kind layout children).
- `cpp/host.cpp` `register_diagram()`: schema registrations and lmdraw-parity validation messages; `cpp/schema.hpp` + `Registrar` carry overload sets.
- `cpp/layout.{hpp,cpp}`: `Shape` gained label/geometry/opacity fields; `draw_shape` routes diagram kinds through `render_shape` and writes the laid-out rect back for anchors.
- `cpp/plot.hpp` `CanvasSlot` holds the lazily created layer pointer; `lmv::Paint::dash_list` (in `cpp/lmvideo.*`) backs arbitrary dash patterns with a phase.
- `tests/diagram_test.cpp`: anchor, dash, elbow-routing, attribute, and post-layout connector-resolution unit checks; `test.sh` sections 15-17 cover refusals, layout composition, the eight ported stills, determinism, and the four animated videos.
- Examples: `flowchart-order-approval`, `etl-pipeline`, `sequence-oauth-refresh`, `state-machines`, `org-chart-grid`, `decision-tree`, `swimlane-process`, `timeline-gantt` (stills) and `etl-pipeline-draw`, `sequence-messages-reveal`, `state-machine-traffic-light`, `flowchart-path-trace` (240-frame videos). `docs/images/lmdraw-lmvideo-flowchart-comparison.png` shows both hosts rendering the same vocabulary.
