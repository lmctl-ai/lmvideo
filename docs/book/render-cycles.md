# A second look: `render="cycles"`

Raster rendering is the fast preview path. It is useful while you are shaping
a scene, checking a camera, or scrubbing through an animation. When the scene
is ready for a more physical image, lmvideo can render the same authored scene
with the CPU Cycles backend:

```lmscript
mkdir("build");
program = _lmcode_ cli="lmvideo";

let scene = scene3d().background("#111923");
scene.camera(camera3d()
  .look_at(4.0, 2.8, 6.0, 0.0, 0.6, 0.0)
  .perspective(42.0, 0.1, 100.0));
scene.directional_light(2.0, 3.0, 4.0, "#ffffff", 2.0);
scene.add(mesh_sphere(1.0, 32, 48),
          material("#d9a441", 0.25, 0.45));

let beauty = scene.render_image(
  960, 540, "cycles", 32, 4, 42, true);
beauty.save("output/sphere-cycles.png");

end _lmcode_
savedsl(program, "output/sphere-cycles.dslb");
```

With a Cycles-enabled `lmvideo` on PATH, save the source as
`sphere-cycles.lms`, compile it, then run the saved bytecode:

```sh
lmscript sphere-cycles.lms
lmvideo --dsl output/sphere-cycles.dslb
```

The five arguments after the dimensions are, in order, the renderer name,
path-tracing samples, maximum bounces, a repeatable random seed, and the
denoise switch (seven arguments total when width and height are included).
`true` asks for CPU Open Image Denoise before the final image conversion. The
returned value is the ordinary lmvideo `Image`, so the same
`text`, `composite`, `save`, and canvas operations used for a raster result can
be applied afterward. The renderer name is explicit: a build without Cycles
reports an error instead of silently substituting a raster image.
Use `render_available("cycles")` when a script needs to check that capability.

In prose, `render="cycles"` names this renderer selection; in lmscript it is
the third positional argument to `Scene3D.render_image`, as shown above.

## Preview first, final second

Keep one `Scene3D` and change only the render call when comparing the two
paths. This makes a raster preview a useful composition check while the Cycles
image answers a different question: how light, reflections, shadows, and
roughness behave when rays are traced through the scene. The camera, meshes,
node transforms, materials, lights, environment, and authored animation time
are shared. A beauty-image pixel comparison is descriptive; small differences
in indirect light and denoising are expected. Geometry comparisons should use
the paired probes and tolerances in the project test suite rather than treating
every shading difference as a scene error.

For a moving scene, set the scene time before each render. `video.time()` is
the authored time in seconds, so the same loop can produce a raster preview or
a Cycles final frame:

```lmscript
mkdir("build");
program = _lmcode_ cli="lmvideo";
video.size(960, 540); video.fps(24); video.frames(48);
video.output("output/turntable.mp4"); video.begin();
let scene = scene3d().background("#09111f");
scene.camera(camera3d().look_at(4.5, 2.5, 5.5, 0.0, 0.0, 0.0)
  .perspective(45.0, 0.1, 100.0));
scene.directional_light(2.0, 3.0, 4.0, "#ffffff", 2.0);
let body = scene.add(mesh_box(1.4, 1.0, 0.6),
                     material("#e76f51", 0.25, 0.45));
for (frame in range(0, video.count())) {
  video.frame();
  body.rotation(0.0, 360.0 * video.t(), 0.0);
  scene.time(video.time());
  let image = scene.render_image(
    video.width(), video.height(), "cycles", 16, 4, 42, true);
  canvas.clear("#09111f");
  canvas.image(image, 0.0, 0.0);
}
video.end();
end _lmcode_
savedsl(program, "output/turntable.dslb");
```

The video source is compiled once and the host renders each frame. For a quick
smoke test, lower the dimensions and frame count in the source; the renderer
selection and scene handoff stay the same.

For long sequences, a render job separates authoring from rendering. Call
`scene.save_job(path, width, height, "cycles", samples, bounces, seed,
denoise)` after constructing the scene, then invoke the native host:

```sh
lmvideo render-job output/scene-job --renderer cycles --output output/frame.png
lmvideo render-job-batch output/scene-frames --renderer cycles \
  --output output/frames
```

Jobs are bounded and versioned. Batch output is written atomically, frame IDs
are numeric and stable, completed frames survive cancellation, and the result
manifest records each frame's settings and checksum. The capabilities command
lists accepted job options and bounds; it does not load or smoke-test the backend:

```sh
lmvideo render-job --capabilities
```

## Choosing settings

Samples reduce noise; bounces allow light to travel through more indirect-light and
reflection paths. Start with a small sample count while framing the shot,
then increase it for the final. A fixed seed makes repeated renders useful for
regression checks. Denoising is usually a good final-image default, but a raw
render can be useful when diagnosing noise or comparing renderer changes.
Resolution and sample count multiply the work. A short video can take minutes
to render. Set `scene.threads(4)` to use four CPU workers where available; keep
the worker count fixed for repeatability checks. CPU Cycles is therefore a final
render path, not a replacement for the quick raster preview. The shipped
release is built with the CPU backend and its pinned dependencies; a lean
development/CI build intentionally omits that SDK. The exact release build
procedure and platform prerequisites belong to the release-build documentation.

The current Cycles profile is deliberately explicit about unsupported input.
Screen-space Gaussian sprites, non-opaque materials, mixed U/V wrapping,
non-unit occlusion maps, and per-object shadow overrides are rejected rather
than dropped. Keep those features on the raster path or simplify the scene
before requesting a Cycles beauty render. B13's depth and normal diagnostics,
for example, remain raster passes while its beauty image uses Cycles.

The practical workflow is simple: author once, preview in raster, render a
small Cycles still, inspect the lighting, then render the final still or
bounded batch. The Phase 6 gallery companions apply that workflow to the
eligible Tier-1 targets, recording the renderer settings, source locks,
repeatability checks, and raster comparison in their content manifest.
