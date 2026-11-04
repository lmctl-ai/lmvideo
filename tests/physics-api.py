#!/usr/bin/env python3
"""Execute the public lmcode physics API, including located refusal paths."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HOST = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/lmvideo").resolve()
COMPILER = Path(os.environ.get("LMSCRIPT_BIN", ROOT / "third_party/compiler/node_modules/.bin/lmscript")).resolve()
SCHEMA = Path(os.environ.get("LMVIDEO_QUALIFY_SCHEMA", ROOT / "schema/lmvideo.dsl.json")).resolve()

def compiler_command(source):
    arguments = [str(COMPILER), str(source)]
    if os.name == "nt" and COMPILER.suffix.lower() in {".cmd", ".bat"}:
        return ["cmd.exe", "/d", "/c", *arguments]
    return arguments


def compile_run(body, work, name):
    source = work / (name + ".lms")
    bytecode = source.with_suffix(".dslb")
    source.write_text(
        "p = _lmcode_ schema=" + json.dumps(str(SCHEMA)) + ";\n"
        + body
        + "\nend _lmcode_\n"
        + "r = run_lmcode(p, {command: [" + json.dumps(str(HOST)) + ", \"--lmcode\", \"$DSLFILE\"]});\n"
        + "if (r.ok) { println(\"PHYSICS_OK\"); } else { println(\"PHYSICS_ERR:\" + r.message); }\n"
    )
    result = subprocess.run(compiler_command(source), cwd=work,
                            capture_output=True, text=True)
    return result, result.stdout + result.stderr, source


def positive(body, work, name):
    result, payload, source = compile_run(body, work, name)
    if result.returncode or "PHYSICS_OK" not in payload:
        raise AssertionError(f"positive {name} failed:\n{source.read_text(encoding='utf-8')}\n{payload}\n{result.stderr}")


def negative(body, phrase, work, name):
    source = work / (name + ".lms")
    bytecode = source.with_suffix(".dslb")
    source.write_text(
        "p = _lmcode_ schema=" + json.dumps(str(SCHEMA)) + ";\n"
        + body
        + "\nend _lmcode_\n"
        + "r = run_lmcode(p, {command: [" + json.dumps(str(HOST)) + ", \"--lmcode\", \"$DSLFILE\"]});\n"
        + "if (r.ok) { println(\"PHYSICS_OK\"); } else { println(\"PHYSICS_ERR:\" + r.message); }\n"
    )
    result = subprocess.run(compiler_command(source), cwd=work,
                            capture_output=True, text=True)
    payload = result.stdout + result.stderr
    if result.returncode or phrase not in payload or "PHYSICS_ERR:" not in payload:
        raise AssertionError(f"negative wrapper {name} failed:\n{source.read_text(encoding='utf-8')}\n{payload}")


with tempfile.TemporaryDirectory(prefix="lmvideo-physics-api-") as directory:
    work = Path(directory)
    still = work / "physics-scene.png"
    presentation = work / "physics-presentation.png"
    positive(
        """
let m = physics_material(0.5, 0.35);
image_assert_true(abs(m.friction() - 0.5) < 0.0001 && abs(m.restitution() - 0.35) < 0.0001);
let w = physics_world(1.0 / 120.0, 42).gravity(0.0, -9.81, 0.0);
let floor = w.box(8.0, 0.25, 8.0, 0.0, -0.125, 0.0, 0.0, m);
let ball = w.sphere(0.35, 0.0, 2.0, 0.0, 1.0, m);
let cube = w.box(0.6, 0.6, 0.6, 1.0, 3.0, 0.0, 1.0, m);
image_assert_true(w.body_count() == 3 && ball.id() == 1 && cube.id() == 2);
let before = ball.y(); w.step(60); image_assert_true(ball.y() < before); image_assert_true(w.time() > 0.4);
image_assert_true(ball.vy() < 0.0 || ball.sleeping());
let cached = w.body(1); image_assert_true(cached.id() == ball.id() && cached.x() == ball.x());
let take = w.bake(61, 2); image_assert_true(take.frames() == 61 && take.body_count() == 3);
let p0 = take.pose(0.0, 1); let pm = take.pose(0.5, 1); let p1 = take.pose(1.0, 1);
image_assert_true(abs(p0.qw()) <= 1.0001 && abs(pm.qw()) <= 1.0001 && abs(p1.qw()) <= 1.0001);
image_assert_true(abs(pm.x() - (p0.x() + p1.x()) * 0.5) < 2.0);
let scene = scene3d(); let node = scene.add(mesh_sphere(0.4, 16, 24), "#ef7d88");
ball.sync(node); pm.sync(node);
let image = scene.render_image(160, 120); image.save(""" + json.dumps(str(still)) + """);
image_assert_true(image.width() == 160 && image.height() == 120);
let p = physics_presentation(320, 240, 42).title("MOVE", 70.0, 40.0, "#f08bad");
p = p.bar("BUILD", 7.0, 10.0, 70.0, 180.0, "#75cad2");
p = p.bullet("Now", 180.0, 130.0, "#facd73");
p = p.kpi("42", 42, 250.0, 190.0, "#a2d9a8");
image_assert_true(p.body_count() > 40); p.step(8); let pi = p.render(); pi.save(""" + json.dumps(str(presentation)) + """);
image_assert_true(pi.width() == 320 && pi.height() == 240);
""", work, "positive")
    if not still.is_file() or not presentation.is_file():
        raise AssertionError("positive physics API did not write rendered images")

    common_material = 'let m=physics_material(0.5,0.3);let w=physics_world(0.01,1);'
    negatives = [
        ('physics_world(0.0,1);', 'fixed timestep'),
        ('physics_world(0.01,-1);', 'seed'),
        ('physics_material(-0.1,0.3);', 'friction'),
        ('physics_material(0.5,1.1);', 'restitution'),
        ('physics_presentation(63,240,1);', 'dimensions'),
        ('physics_presentation(320,1921,1);', 'dimensions'),
        (common_material + 'w.box(0.0,1.0,1.0,0.0,0.0,0.0,0.0,m);', 'box width'),
        (common_material + 'w.sphere(-1.0,0.0,0.0,0.0,1.0,m);', 'sphere radius'),
        (common_material + 'w.sphere(1.0,0.0,0.0,0.0,-1.0,m);', 'mass'),
        (common_material + 'w.gravity(0.0,sqrt(-1.0),0.0);', 'gravity'),
        (common_material + 'w.body(-1);', 'index'),
        (common_material + 'w.step(-1);', 'count'),
        (common_material + 'w.step(10001);', 'count'),
        (common_material + 'let b=w.sphere(1.0,0.0,0.0,0.0,1.0,m);b.velocity(sqrt(-1.0),0.0,0.0);', 'velocity'),
        (common_material + 'let b=w.sphere(1.0,0.0,0.0,0.0,1.0,m);b.impulse(100001.0,0.0,0.0);', 'impulse'),
        (common_material + 'w.box(1.0,1.0,1.0,0.0,0.0,0.0,0.0,m);w.planar(true);', 'planar'),
        (common_material + 'w.bake(1,1);', 'frames'),
        (common_material + 'w.bake(61,10001);', 'stride'),
        (common_material + 'w.sphere(1.0,0.0,0.0,0.0,1.0,m);w.bake(512,20);', '10000'),
        (common_material + 'for(i in range(0,513)){w.sphere(0.1,0.0,0.0,0.0,1.0,m);}w.bake(512,1);', 'pose budget'),
        (common_material + 'w.sphere(1.0,0.0,0.0,0.0,1.0,m);w.bake(2,1).pose(-0.1,0);', 'time'),
        (common_material + 'w.sphere(1.0,0.0,0.0,0.0,1.0,m);w.bake(2,1).pose(0.5,2);', 'body'),
        (common_material + 'w.sphere(1.0,0.0,0.0,0.0,1.0,m);let s=scene3d();let n=s.group();s.clear();w.body(0).sync(n);', 'detached'),
        (common_material + 'w.sphere(1.0,0.0,0.0,0.0,1.0,m);let s=scene3d();let n=s.add(mesh_box(1.0,1.0,1.0),"#fff");let p=s.group();n.parent(p);w.body(0).sync(n);', 'parented'),
        (common_material + 'w.sphere(1.0,0.0,0.0,0.0,1.0,m);let s=scene3d();let n=s.add(mesh_box(1.0,1.0,1.0),"#fff");n.keyframe(0.0,0.0,0.0,0.0,0.0,0.0,0.0,1.0,1.0,1.0);w.body(0).sync(n);', 'keyframed'),
        ('let p=physics_presentation(320,240,1);p.title("",1.0,1.0,"#fff");', 'text'),
        ('let p=physics_presentation(320,240,1);p.kpi("x",65,100.0,100.0,"#fff");', 'count'),
        ('let p=physics_presentation(320,240,1);p.title("A",0.0,0.0,"bad");', 'color'),
        ('let p=physics_presentation(320,240,1);p.title("A",1000.0,1000.0,"#fff");', 'position'),
        ('let p=physics_presentation(320,240,1);p.bullet("x",sqrt(-1.0),1.0,"#fff");', 'finite'),
    ]
    for index, (body, phrase) in enumerate(negatives):
        negative(body, phrase, work, f"negative-{index:02d}")

print("PASS physics lmcode API, immutable replay, Node3D sync, presentation render, and 28 refusals")
