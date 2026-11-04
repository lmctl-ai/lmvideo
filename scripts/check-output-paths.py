#!/usr/bin/env python3
"""Exercise every public file writer with native Windows and Unicode paths."""
import argparse, json, os, shutil, subprocess
from pathlib import Path

def run(cmd, cwd, env):
    argv = [str(x) for x in cmd]
    if os.name == "nt" and Path(argv[0]).suffix.lower() in (".cmd", ".bat"):
        argv = ["cmd.exe", "/d", "/c", *argv]
    p = subprocess.run(argv, cwd=cwd, env=env, text=True, capture_output=True, encoding="utf-8", timeout=300)
    if p.returncode:
        raise RuntimeError("command failed: " + " ".join(argv) + "\n" + p.stdout[-4000:] + p.stderr[-4000:])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", type=Path, required=True)
    ap.add_argument("--compiler", type=Path, required=True)
    ap.add_argument("--work", type=Path, required=True)
    a = ap.parse_args()
    host = a.host.resolve(strict=True); compiler = a.compiler.resolve(strict=True)
    work = a.work.resolve(); work.mkdir(parents=True, exist_ok=False)
    tools = work / "tools"; tools.mkdir()
    alias = tools / ("lmvideo.exe" if os.name == "nt" else "lmvideo")
    try: alias.symlink_to(host)
    except (OSError, NotImplementedError): shutil.copy2(host, alias)
    env = dict(os.environ); env["PATH"] = str(tools) + os.pathsep + env.get("PATH", "")
    checks = {"png": (b"\x89PNG\r\n\x1a\n", 32), "mp4": (b"ftyp", 16), "glb": (b"glTF", 12), "stl": (None, 84)}
    root = work / "native paths"; absolute = root / "space Ω" / "absolute"; absolute.mkdir(parents=True)
    (work / "relative space Ω").mkdir(parents=True)
    q = lambda value: json.dumps(str(value), ensure_ascii=False)
    def emit(label, values):
        src = work / (label + ".lms")
        src.write_text(
            'p = _lmcode_ cli="lmvideo";\n'
            f'video.size(32,24); video.frames(1); video.output({q(values["mp4"])}); video.begin(); video.frame(); canvas.clear("#15304a"); video.save_frame({q(values["png"])}); video.end();\n'
            f'let pixels=surface(32,24);pixels.canvas().clear("#254064");pixels.image().save({q(values["png"])});\n'
            f'let mesh=mesh_sphere(0.5,8,12); mesh.save({q(values["stl"])}); mesh.save({q(values["glb"])});\n'
            'end _lmcode_\n'
            'r=run_lmcode(p); assert(r.ok);\n',
            encoding="utf-8", newline="\n")
        run([compiler, src], work, env)
    def verify(values):
        for name, (magic, minimum) in checks.items():
            output = Path(values[name]); output = output if output.is_absolute() else work / output
            data = output.read_bytes()
            if name == "stl":
                if len(data) < 84: raise ValueError("stl output is too short")
                count = int.from_bytes(data[80:84], "little")
                if count == 0 or len(data) != 84 + 50 * count: raise ValueError("stl output has invalid binary size")
            elif len(data) < minimum or magic not in data[:32]:
                raise ValueError(f"{name} output is malformed")
    absolute_values = {name: absolute / ("repeat." + name) for name in checks}
    emit("absolute-first", absolute_values); verify(absolute_values)
    emit("absolute-repeat", absolute_values); verify(absolute_values)
    relative_values = {name: Path("relative space Ω") / ("repeat." + name) for name in checks}
    emit("relative-first", relative_values); verify(relative_values)
    emit("relative-repeat", relative_values); verify(relative_values)
    print("PASS output paths: PNG MP4 GLB STL; absolute, relative, spaces, Unicode, replacement")

if __name__ == "__main__":
    main()
