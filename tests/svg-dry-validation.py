#!/usr/bin/env python3
"""Require lmvideo's SVG importer to validate input during lmcode dry runs."""
from pathlib import Path
import json, os, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[1]
host = str(Path(sys.argv[1]).resolve())
compiler = sys.argv[2]
if os.name == "nt":
    compiler = subprocess.check_output(["bash", "-c", 'cygpath -u "$1"', "svg-dry", compiler], encoding="utf-8").strip()
schema = Path(os.environ.get("LMVIDEO_QUALIFY_SCHEMA", root / "schema" / "lmvideo.dsl.json"))
valid = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><rect width="10" height="10"/></svg>'
invalid = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><foreignObject width="1" height="1"/></svg>'

with tempfile.TemporaryDirectory(prefix="lmvideo-svg-dry-") as temporary:
    work = Path(temporary)
    (work / "build").mkdir()
    for label, svg, should_succeed in (("valid", valid, True), ("foreign-object", invalid, False)):
        source = (
            "svg_text = " + json.dumps(svg) + ";\n"
            + "p = _lmcode_ schema=" + json.dumps(str(schema)) + ";\n"
            + "let doc = svg(${svg_text}); video.size(10, 10); video.frames(1); video.output(\"build/svg-dry.png\"); video.begin(); video.frame(); canvas.svg(doc, 0.0, 0.0, 10.0, 10.0); video.end(); $return.valid = true;\n"
            + "end _lmcode_\n"
            + "r = run_lmcode(p, {command:" + json.dumps([host, "--lmcode", "$DSLFILE"]) + "});\n"
            + "println(json_stringify(r));\n"
        )
        script = work / f"{label}.lms"
        script.write_text(source, encoding="utf-8")
        result = subprocess.run(
            ["bash", "-c", 'exec "$@"', "svg-dry", compiler, "--dry-run", "--plan-format", "json", str(script)],
            cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8",
        )
        assert result.returncode == 0, (label, result.stdout)
        plan = json.loads(result.stdout)
        entries = [item for item in plan["entries"] if item["kind"] == "lmcode-host-dry"]
        assert len(entries) == 1, (label, result.stdout)
        envelope = entries[0]["envelope"]
        assert envelope.get("dry") is True, (label, envelope)
        assert envelope.get("ok") is should_succeed, (label, envelope, result.stdout)
        if not should_succeed:
            assert "foreignObject" in envelope.get("message", ""), (label, envelope)
        assert not list((work / "build").rglob("*.png")), (label, "dry run wrote a PNG")
        print("PASS SVG dry validation:", label)
