#!/usr/bin/env python3
"""Run the shared compact SVG through lmvideo and the pinned lmoffice host."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
COMPILER = sys.argv[1]
VIDEO_TAG = os.environ.get("LMVIDEO_QUALIFY_TAG", "v0.1.107")
VIDEO_PIN = "github:lmctl-ai/lmvideo@" + VIDEO_TAG
OFFICE_PIN = "github:lmctl-ai/lmoffice@v0.1.34"
EXAMPLE = ROOT / "examples" / "svg-compact-pptx.lms"

if os.name == "nt":
    COMPILER = subprocess.check_output(
        ["bash", "-c", 'cygpath -u "$1"', "svg-pptx", COMPILER], encoding="utf-8"
    ).strip()


def run_compiler(args, cwd, env):
    if os.name == "nt":
        command = ["bash", "-c", 'exec "$@"', "svg-pptx", COMPILER, *map(str, args)]
    else:
        command = [COMPILER, *map(str, args)]
    return subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True)


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def prepare_source(text):
    pattern = r'(?ms)^svg_data = _xml_ profile="svg-compact@1";\r?\n(.*?)\r?\nend _xml_'
    match = re.search(pattern, text)
    require(match is not None, "example must keep one svg-compact@1 XML block")
    markup = match.group(1)
    text = re.sub(pattern, "svg_data = " + json.dumps(markup) + ";", text, count=1)
    text = text.replace("svg_text = svg_data.toString();", "svg_text = svg_data;")
    return text, markup


def plan_for(compiler, source, work, env):
    script = work / "probe.lms"
    script.write_text(source, encoding="utf-8")
    result = run_compiler(["--dry-run", "--plan-format", "json", script], work, env)
    require(result.returncode == 0, result.stdout + result.stderr)
    return json.loads(result.stdout)


with tempfile.TemporaryDirectory(prefix="lmvideo-svg-pptx-") as temporary:
    work = Path(temporary)
    private = work / "private"
    tools = work / "tool-cache"
    cache = work / "xdg-cache"
    for directory in (private, tools, cache):
        directory.mkdir(parents=True)
    private_home = private / "home"
    private_home.mkdir()
    env = dict(os.environ)
    env["HOME"] = str(private_home)
    env["LMCTL_HOME"] = str(private / "lmctl-home")
    env["LMSCRIPT_TOOL_CACHE"] = str(tools)
    env["XDG_CACHE_HOME"] = str(cache)

    installed_hosts = {}
    candidate = os.environ.get("LMVIDEO_CANDIDATE_BIN")
    candidate_path = None
    if candidate:
        candidate_source = Path(candidate).resolve(strict=True)
        expected = "lmvideo " + VIDEO_TAG.removeprefix("v")
        version = subprocess.run([str(candidate_source), "--version"], text=True, capture_output=True, check=True).stdout.strip()
        require(version == expected, f"candidate version {version!r} differs from {expected!r}")
        bin_dir = work / "bin"
        bin_dir.mkdir()
        candidate_path = bin_dir / ("lmvideo.exe" if os.name == "nt" else "lmvideo")
        shutil.copy2(candidate_source, candidate_path)
        if os.name != "nt":
            candidate_path.chmod(0o755)
        env["PATH"] = str(bin_dir) + os.pathsep + env.get("PATH", "")
    # The public workflow preinstalls pinned hosts for the full example suite.
    # Remove those host directories in this fresh-install subtest so lmoffice
    # is resolved into the private cache, while retaining candidate lmvideo.
    cleaned_path = []
    for entry in env.get("PATH", "").split(os.pathsep):
        directory = Path(entry or ".")
        if candidate_path and directory.resolve() == candidate_path.parent.resolve():
            cleaned_path.append(entry)
            continue
        if any((directory / name).is_file() for name in ("lmvideo", "lmvideo.exe", "lmoffice", "lmoffice.exe")):
            continue
        cleaned_path.append(entry)
    env["PATH"] = os.pathsep.join(cleaned_path)
    for name, pin in (("lmvideo", VIDEO_PIN), ("lmoffice", OFFICE_PIN)):
        if name == "lmvideo" and candidate_path:
            host_path = candidate_path
        else:
            installed = run_compiler(["install", pin], work, env)
            require(installed.returncode == 0, installed.stdout + installed.stderr)
            host_path = None
            for line in installed.stdout.splitlines():
                if line.startswith("path: "):
                    host_path = Path(line[6:])
        require(host_path is not None and host_path.is_file(), f"{name} installation path is missing")
        require(str(host_path).startswith(str(work)), f"{name} must be installed in the test-private cache")
        installed_hosts[name] = host_path
    office_path = installed_hosts["lmoffice"]

    # Resolve both pinned schemas once; subsequent check/dry runs use cached snapshots.
    discovery = work / "discover-host-schemas.lms"
    discovery.write_text(
        'video = load_schema(cli="lmvideo" install="' + VIDEO_PIN + '");\n'
        'office = load_schema(cli="lmoffice pptx" install="github:lmctl-ai/lmoffice@v0.1.34");\n',
        encoding="utf-8",
    )
    discovered = run_compiler([discovery], work, env)
    require(discovered.returncode == 0, discovered.stdout + discovered.stderr)

    original = EXAMPLE.read_text(encoding="utf-8")
    original = re.sub(r'github:lmctl-ai/lmvideo@[^"]+', VIDEO_PIN, original)
    source, markup = prepare_source(original)
    script = work / "svg-compact-pptx.lms"
    script.write_text(source, encoding="utf-8")

    checked = run_compiler(["--check", script], work, env)
    require(checked.returncode == 0, checked.stdout + checked.stderr)

    dry = plan_for(COMPILER, source, work, env)
    calls = [entry["envelope"] for entry in dry["entries"] if entry["kind"] == "lmcode-host-dry"]
    require(len(calls) == 2, str(dry))
    require(all(call.get("dry") is True and call.get("ok") is True for call in calls), str(dry))
    require(not (work / "build" / "svg-compact.png").exists(), "dry run wrote a PNG")
    require(not (work / "build" / "svg-compact.pptx").exists(), "dry run wrote a PowerPoint file")

    for label, bad_markup, fragment in (
        ("unsupported element", markup.replace("<circle", "<foreignObject width=\"1\" height=\"1\"/><circle", 1), "foreignObject"),
        ("unsupported attribute", markup.replace("<rect width", "<rect class=\"bad\" width", 1), "class"),
    ):
        bad = source.replace(json.dumps(markup), json.dumps(bad_markup), 1)
        result_plan = plan_for(COMPILER, bad, work, env)
        failed_calls = [entry["envelope"] for entry in result_plan["entries"] if entry["kind"] == "lmcode-host-dry"]
        require(len(failed_calls) == 1, f"{label}: {result_plan}")
        envelope = failed_calls[0]
        require(envelope.get("dry") is True and envelope.get("ok") is False, f"{label}: {result_plan}")
        require(fragment in envelope.get("message", ""), f"{label}: {envelope}")
        require(envelope.get("line", 0) > 0 and envelope.get("column", 0) > 0, f"{label} has no source location: {envelope}")

    real = run_compiler([script], work, env)
    require(real.returncode == 0, real.stdout + real.stderr)
    png = work / "build" / "svg-compact.png"
    pptx = work / "build" / "svg-compact.pptx"
    require(png.is_file() and pptx.is_file(), f"missing output: {real.stdout}{real.stderr}")
    png_bytes = png.read_bytes()
    require(png_bytes[:8] == b"\x89PNG\r\n\x1a\n", "lmvideo output is not PNG")
    require(struct.unpack(">II", png_bytes[16:24]) == (160, 100), "lmvideo PNG dimensions differ")
    for x, y, expected in ((20, 20, (18, 52, 86, 255)), (120, 50, (239, 71, 111, 255)), (85, 50, (237, 242, 247, 255))):
        pixel = subprocess.check_output([sys.executable, str(ROOT / "tools" / "pngpx.py"), str(png), str(x), str(y)], text=True)
        require(tuple(map(int, pixel.split())) == expected, f"pixel ({x},{y}): {pixel.strip()}")

    inspected = subprocess.run([str(office_path), "inspect", str(pptx)], cwd=work, text=True, capture_output=True)
    require(inspected.returncode == 0, inspected.stdout + inspected.stderr)
    summary = json.loads(inspected.stdout)["result"]
    require(summary.get("valid") is True and summary.get("slides") == 1, str(summary))
    require("One XML drawing, two hosts" in summary.get("text", ""), str(summary))
    require("compact SVG is rendered by lmvideo" in summary.get("text", ""), str(summary))
    with zipfile.ZipFile(pptx) as archive:
        require(archive.testzip() is None, "PowerPoint ZIP integrity failed")
        slide_names = [name for name in archive.namelist() if re.fullmatch(r"ppt/slides/slide\d+\.xml", name)]
        require(len(slide_names) == 1, str(slide_names))
        slide = ET.fromstring(archive.read(slide_names[0]))
        ns = {
            "p": "http://schemas.openxmlformats.org/presentationml/2006/main",
            "a": "http://schemas.openxmlformats.org/drawingml/2006/main",
        }
        require(len(slide.findall(".//p:pic", ns)) == 1, "slide does not contain one picture")
        text_nodes = [node.text or "" for node in slide.findall(".//a:t", ns)]
        require(any("One XML drawing" in text for text in text_nodes), str(text_nodes))
        media_names = [name for name in archive.namelist() if name.startswith("ppt/media/")]
        require(any(hashlib.sha256(archive.read(name)).digest() == hashlib.sha256(png_bytes).digest() for name in media_names), "embedded picture differs from the lmvideo PNG")

    print(f"PASS compact XML SVG -> lmvideo {VIDEO_TAG} PNG -> lmoffice v0.1.34 PowerPoint")
    print(f"PNG: {png.stat().st_size} bytes, 160x100, sha256={hashlib.sha256(png_bytes).hexdigest()}")
    print(f"PPTX: {pptx.stat().st_size} bytes; inspect valid, one slide, one matching embedded picture, expected text")
    print("PASS dry run: two successful dry envelopes, no files; invalid element/attribute controls reject with location")
