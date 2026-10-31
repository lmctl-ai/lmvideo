# Migrating from lmdraw, lmmagnum, lmfilament and lmimg

The four legacy authoring hosts are maintenance-only: critical fixes, no new
features. lmvideo is the single canonical host, and `scripts/migrate-legacy.py`
is the bridge. It rewrites `_lmcode_`/`_dsl_` blocks declared with
`cli="lmdraw"`, `cli="lmimg"`, `cli="lmmagnum"` or `cli="lmfilament"` to the
canonical lmvideo API, or emits precise located diagnostics for the calls that
have no exact mapping. It never silently changes semantics, units or
arguments: every rewrite is recorded in the report, and approximate mappings
come with a warning diagnostic.

```sh
python3 scripts/migrate-legacy.py old-script.lms -o migrated.lms --report report.json
```

Diagnostics look like `old-script.lms:8:46: error LMV7324 [lmmagnum:material]
Material.specular: Blinn-Phong white specular highlights have no exact GGX PBR
equivalent hint: choose roughness (and possibly clearcoat) by eye`. Exit code
is 1 when any error is diagnosed; the output is not written unless
`--keep-going` is passed, and even then a block containing an error stays
byte-identical so it keeps running on its pinned legacy host. `--check`
verifies a committed migration against a fresh run; `--list-rules` dumps the
full transformation table as JSON.

## How the rewrite works

Only actual DSL blocks are touched. Strings, comments, splices, outer
lmscript code and blocks for other tools are preserved byte-for-byte (the
scanner masks string/comment spans before matching). The old
`install="github:lmctl-ai/..."` pin is dropped with warning LMV7010 — pin the
lmvideo runtime explicitly. Fluent chains keep working because the legacy
return-type graph is tracked: `scene.sphere(0.6, body).translate(0.0, 1.5,
0.0)` becomes `scene3d-add(...).position(...)` in one expression.

## lmdraw -> lmvideo

lmvideo's retained Canvas drawing *is* the lmdraw vocabulary, so shape and
diagram calls map `scene.X(...)` -> `canvas.X(...)` with identical arguments,
including `connector` overloads, `linear_gradient` + `url(#id)` fills, PNG
data-URI `image`, `measure_text` and the `set_attr` allowlist (lmdraw's
attribute set is a subset of lmvideo's). Three statement-level changes:

| lmdraw | lmvideo |
|---|---|
| `scene.canvas(w, h, bg);` | `canvas.target(surface(w, h)); canvas.clear(bg);` |
| `scene.render_png(p, w, h, scale);` | `canvas.redraw(); canvas.png(p, w, h, scale);` |
| `scene.load_font(family, path, bold);` | `let <fresh> = font_load(path);` + warning LMV7101 (lmvideo addresses loaded fonts as Typeface values, not family registrations) |

When a block draws text, the migrator injects the pinned Noto Sans/Bold/Math
`font_load(url, sha256)` bootstrap and `canvas.typeface(...)` so migrated
diagrams keep lmdraw's fonts.

## lmimg -> lmvideo

lmimg's mutable fluent Image operations are lmvideo's migration baseline, so
`Image`, `Curve`, `Layers` (-> `Layout`), `Montage` (-> `Layout` sheet mode)
and `Animation` (-> `Timeline`) methods pass through unchanged — including
GIF centisecond delays, the ten blend modes, and `levels`/`hsl`/`sepia`
units. The `img.` root object splits into free functions:

| lmimg | lmvideo |
|---|---|
| `img.open(p)` | `image_file(p)` |
| `img.canvas(w,h,r,g,b,a)` | `image_create(w,h,r,g,b,a)` |
| `img.pattern(w,h,R,G,B,A)` | `image_pattern(w,h,R,G,B,A)` |
| `img.hald(level)` / `img.curve()` | `image_hald(level)` / `image_curve()` |
| `img.layers(w,h)` / `img.montage()` | `layout_create(w,h)` / `layout_sheet()` |
| `img.animation()` | `timeline_create()` |
| `img.panel(a,b,gap)` / `img.text_width(t,s)` | `image_panel(a,b,gap)` / `image_text_width(t,s)` |
| `img.assert_true/assert_close` | `image_assert_true/image_assert_close` |
| `img.print(s)` | `out.print(s)` |

Errors: LMV7201 `img.eval` (no constant-expression evaluator; expressions
still work inside `image_pattern`), LMV7202 `Image.property`, LMV7203
`Image.metadata` (no ImageMagick property bag in lmvideo). Note:
`Image.morphology` supports only `erode`/`dilate`/`median`, and text on
images uses a different font engine — glyph pixels differ.

## lmmagnum -> lmvideo

The renderer changes from lmmagnum's gamma-space Blinn-Phong to lmvideo's
GGX PBR CPU rasterizer, so 3D output is *declared-equivalent* (geometry,
camera, colors), not pixel-equal. Verified transformations:

| lmmagnum | lmvideo | note |
|---|---|---|
| `scene.background(r,g,b)` | `background("#rrggbb")` | float bytes are sRGB display values |
| `scene.camera(fov, eye..., target...)` | `camera3d().perspective(fov, 0.05, 10000.0).look_at(...)` | fov stays **vertical** degrees (the code says fovY despite an older doc note); near/far preserved |
| `scene.directional_light(dx,dy,dz,i)` | `directional_light(-dx,-dy,-dz, "#ffffff", i)` | lmmagnum's vector is the travel direction; lmvideo's points **toward** the source |
| `scene.shadows(true/false)` | `shadows(2048, 0.0003, 0.001, 1)` / `shadows_off()` | warning LMV7311: bias semantics differ |
| `scene.cube/box/sphere/cylinder/cone(s, mat)` | `scene3d().add(mesh_box/mesh_sphere(r,24,48)/mesh_cylinder(r,h,48)/..., mat)` | tessellation made explicit |
| `scene.plane(size, mat)` | `add(mesh_plane(size, size), mat)` | lmmagnum's plane is XY/+Z, lmvideo's is XZ/+Y; a chained `rotate(-90,0,0)` is dropped automatically |
| `.translate/.rotate/.scale` | `.position/.rotation/.scale` | SET semantics, degrees, T·Rz·Ry·Rx·S on both |
| `material().color(r,g,b)` | `material().base_color("#rrggbb")` | literal floats only (LMV7301 otherwise) |
| `material().roughness(f)` | kept | warning LMV7323: lmmagnum never applied roughness; lmvideo honors it |
| `scene.render_png(p,w,h)` | `render_image(w,h,2).save(p)` | 2x2 supersampling becomes `samples=2`; warning LMV7312 |
| `scene.texture(w,h)` + `pixel(x,y,r,g,b)` | `image_create(w,h,"#ffffff")` + `set_pixel(x,y,r,g,b,1.0)` | legacy procedural textures start **opaque white** |
| `scene.texture_checker(size,cell,c1,c2)` | `texture_checker(#c1,#c2,size/cell).bake(size,size,"srgb")` | literal size/cell required; cell phase on primitive UVs can differ (LMV7333) |
| `scene.texture_file(p)` | `texture_file(p,"srgb")` | lmmagnum composites alpha over white; lmvideo keeps it |

Located errors (never faked): LMV7324/LMV7325 `specular`/`shininess` (no
exact PBR equivalent — choose roughness by eye), LMV7316 icosphere, LMV7317
capsule (lmmagnum silently ignores its `length` argument upstream), LMV7318
ring, LMV7319 `tube_between` (lmvideo `mesh_tube_spline` is uncapped),
LMV7320 voxel `text` (use `mesh_text` with a real typeface), LMV7307/7308/
7309 scene-level OBJ/STL/PLY export (use `export_gltf` or per-mesh
`Mesh3D.save`), LMV7314 the `accumulate` motion-blur idiom, LMV7306
`Light.intensity` (no light handles), LMV7327 per-shape `texture` (bind via
`Material.albedo`).

## lmfilament -> lmvideo

lmfilament's CPU preview is Lambert with a `1-exp(-1.3c)` tone map; lmvideo
is GGX PBR. Exact mappings: `camera` (vertical-fov degrees, fixed near/far
0.1/1000), `plane` (XZ/+Y on both), `box/cylinder/cone/mesh_obj`, Node/Part
transforms, `skybox(r,g,b)` (linear floats are sRGB-encoded into
`background("#hex")`), `sun(i)` and `directional_light(r,g,b,lux,dx,dy,dz)`
(negated direction, `min(1.5, lux/110000)` preview law, color gamma-encoded),
`sphere(r, seg)` -> `mesh_sphere(r, seg, 2*seg)`, textures (v=0 top on both).

Located errors: LMV7402 `fil.material` and LMV7403 `fil.material_file` compile
the Filament shading language via matp/filamat — there is no mechanical
mapping to lmvideo's renderer-neutral material IR, and `.filamat` package
bytes (LMV7414 `package_size`, LMV7415 `Material.save`) are never
reinterpreted as portable packs. LMV7410 `point_light` (incompatible falloff
law), LMV7411 `render_frames` (noop-backend pumping produces nothing),
LMV7406 the vertex-colored `triangle()`, LMV7416 named `set_float*`/
`set_float4` parameters outside `baseColor`/`color`/`roughness`/`metallic`,
LMV7417 non-albedo `set_texture`.

## Worked example: what a migration looks like

A lmmagnum block:

```lmscript
p = _lmcode_ cli="lmmagnum" install="github:lmctl-ai/lmmagnum@latest";
scene.background(0.12, 0.13, 0.16);
let body = material().color(0.7, 0.72, 0.78);
scene.box(1.4, 1.8, 1.0, body).translate(0.0, 0.0, 0.0);
scene.camera(35.0, 4.0, 2.5, 7.0, 0.0, 0.2, 0.0);
scene.directional_light(-0.5, -1.0, -0.6, 0.9);
scene.render_png("images/model.png", 640, 640);
end _lmcode_
```

becomes (modulo the injected `let __lmvideo_scene = scene3d();` preamble):

```lmscript
p = _lmcode_ cli="lmvideo";
let __lmvideo_scene = scene3d();
__lmvideo_scene.background("#1f2129");
let body = material().base_color("#b3b8c7");
__lmvideo_scene.add(mesh_box(1.4, 1.8, 1.0), body).position(0.0, 0.0, 0.0);
__lmvideo_scene.camera(camera3d().perspective(35.0, 0.05, 10000.0).look_at(4.0, 2.5, 7.0, 0.0, 0.2, 0.0));
__lmvideo_scene.directional_light(0.5, 1.0, 0.6, "#ffffff", 0.9);
__lmvideo_scene.render_image(640, 640, 2).save("images/model.png");
end _lmcode_
```

The full machine-readable rule table (every legacy method classified as
rewrite, pass-through or error, with its note) is
`python3 scripts/migrate-legacy.py --list-rules`. Before/after fixtures that
run on both hosts ship with the public repository under
`docs/book/migration-fixtures/`.
