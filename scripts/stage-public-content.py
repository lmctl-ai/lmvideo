#!/usr/bin/env python3
"""Create a local public-content stage from a reviewed candidate tree.

This never pushes, publishes, or changes the source tree. It keeps private
provenance in a sidecar report and emits only public-safe demo metadata.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


PRIVATE = re.compile(r"/home/|/tmp/|lmctlhq|durable-memory/|(?:^|/)cpp/|(?:^|/)tests/|sdk\.lock|schema/lmvideo\.dsl\.json", re.I)
def write_lf(path: Path, text: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)


TEXT = {".json", ".lms", ".md", ".txt", ".yml", ".yaml", ".sh", ".svg"}


def safe_string(value: str) -> str:
    value = value.replace("https://github.com/lmctlhq/lmvideo", "runtime:lmvideo")
    value = value.replace("lmctlhq/lmvideo", "reviewed-development-runtime")
    value = re.sub(r"/(?:home|tmp)/[^\s\"\'<>]+", lambda m: "artifact:" + m.group(0).rstrip(",").split("/")[-1], value)
    value = value.replace("durable-memory/", "review-record:")
    value = value.replace("scripts/showcase/", "witness:")
    value = value.replace("tests/", "witness:")
    value = value.replace("cpp/", "runtime-component:")
    value = value.replace("sdk.lock", "runtime-pin")
    value = value.replace("schema/lmvideo.dsl.json", "runtime-schema")
    return value


def scrub(value):
    if isinstance(value, str):
        return safe_string(value)
    if isinstance(value, list):
        return [scrub(item) for item in value]
    if isinstance(value, dict):
        result = {}
        private_source = "lmctlhq" in str(value.get("source_repository", "")).lower()
        for key, item in value.items():
            if private_source and key in {"source_repository", "source_commit"}:
                result["runtime_provenance"] = "reviewed-development-runtime"
                continue
            # These fields identify private implementation/build provenance;
            # retain the fact that a witness exists without exposing its path.
            if key in {"implementation_commit", "implementation_repository", "private_commit", "private_repository", "workdir", "repeat_workdir", "workdirs", "command", "stdout", "stderr", "implementation_cpp_tree", "runtime_source_commit"}:
                result["runtime_provenance"] = "reviewed-development-runtime"
                continue
            result[safe_string(key)] = scrub(item)
        return result
    return value


def copy_tree(source: Path, stage: Path) -> None:
    if stage.exists():
        raise SystemExit(f"refusing to overwrite existing stage: {stage}")
    if any(p.is_symlink() for p in source.rglob("*")):
        raise SystemExit("source stage contains a symlink")
    shutil.copytree(source, stage, symlinks=False)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("stage", type=Path)
    args = parser.parse_args()
    originals = {p.relative_to(args.source).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in args.source.rglob("*") if p.is_file()}
    copy_tree(args.source.resolve(), args.stage.resolve())
    template = Path(__file__).resolve().parents[1] / "packaging/public-templates/lmvideo"
    required_templates = [template / "LICENSE", template / "LICENSE-examples", template / "THIRD_PARTY_NOTICES.md", template / "docs/third-party-licenses/cycles/manifest.json"]
    if not all(path.is_file() for path in required_templates):
        raise SystemExit("missing packaging/public-templates/lmvideo licence template or closure manifest")
    for path in required_templates[:3]:
        shutil.copyfile(path, args.stage / path.relative_to(template))
    shutil.copytree(template / "docs/third-party-licenses", args.stage / "docs/third-party-licenses", dirs_exist_ok=True)
    if (template / "examples").is_dir():
        shutil.copytree(template / "examples", args.stage / "examples", dirs_exist_ok=True)
    # Current B27 source/metadata live in the private examples tree. Replace
    # only an existing peer chapter, then refresh every catalog hash below.
    for relative in ("examples/showcase/bench-27-cad-preview-cycles-twin.lms",
                     "examples/showcase/scenes/b27.json"):
        current = Path(__file__).resolve().parents[1] / relative
        if (args.stage / relative).is_file():
            shutil.copyfile(current, args.stage / relative)
    readme=args.stage / "README.md"
    if readme.is_file():
        write_lf(readme, readme.read_text(encoding="utf-8").replace("Use the released host on your platform.", "v0.1.36 CPU binaries include Cycles for Linux x86_64/arm64, macOS x86_64/arm64 (13 or newer), and Windows x86_64 (10 or newer). Linux requires glibc2.35 or newer; each platform needs its standard tar utility. FFmpeg/ffprobe are user-supplied for video output."))
    private = {}
    svg = Path(__file__).resolve().parents[1] / "tests/golden/scene.svg"
    # The public SVG example must run from an empty directory without tests/.
    # Only replace it when the candidate still carries the tests/-dependent
    # form; the already-published standalone copy keeps its current spelling.
    example = args.stage / "examples/svg.lms"
    if example.exists() and "tests/" in example.read_text(encoding="utf-8"):
        fixed = Path(__file__).resolve().parents[1] / "examples/svg.lms"
        if not fixed.is_file():
            raise SystemExit("missing fixed public SVG example")
        shutil.copyfile(fixed, example)
    extra = args.stage / "examples/svg-input.svg"
    if extra.exists():
        extra.unlink()
    cad_pin = json.loads((Path(__file__).resolve().parents[1] / "toolchain.lock.json").read_text(encoding='utf-8'))["cad"]
    def pin_cad_metadata(value):
        if isinstance(value, list):
            return [pin_cad_metadata(item) for item in value]
        if not isinstance(value, dict):
            return value
        value = {key: (item if key == "previous_release_provenance" else pin_cad_metadata(item))
                 for key, item in value.items()}
        if value.get("name") == "lmcad" and value.get("asset") in cad_pin["assets"]:
            digest = cad_pin["assets"][value["asset"]]
            if value.get("asset_sha256") != digest:
                value["previous_release_provenance"] = {key: value[key] for key in
                    ("install", "release_url", "asset_sha256", "verified_sha256") if key in value}
            value.update(install="github:lmctl-ai/lmcad@v" + cad_pin["version"],
                         release_url="https://github.com/lmctl-ai/lmcad/releases/tag/v" + cad_pin["version"],
                         asset_sha256=digest)
            if "verified_sha256" in value:
                value["verified_sha256"] = digest
        return value
    # Normalize every staged CAD example and its manifest before refreshing hashes.
    # The current migrated release line is 0.1.5, despite the older 0.1.72 tag.
    for cad in args.stage.rglob("*"):
        if cad.is_file() and cad.suffix in {".lms", ".json", ".md"} and not cad.relative_to(args.stage).as_posix().startswith("docs/third-party-licenses/"):
            original = cad.read_text(encoding="utf-8")
            if cad.suffix == ".json":
                pinned = pin_cad_metadata(json.loads(original))
                # Keep prior release provenance immutable when replacing install tokens.
                def selectors(value):
                    if isinstance(value, str):
                        return re.sub(r"lmcad([ @])(?:v0\.1\.(?:4|72)|latest)(?![\w.])", r"lmcad\1v0.1.5", value)
                    if isinstance(value, list): return [selectors(item) for item in value]
                    if isinstance(value, dict): return {key: item if key == "previous_release_provenance" else "v0.1.5" if key == "lmcad" and item in ("v0.1.4", "v0.1.72", "latest") else selectors(item) for key, item in value.items()}
                    return value
                write_lf(cad, json.dumps(selectors(pinned), indent=2) + "\n")
                continue
            updated = re.sub(r"lmcad@(?:v0\.1\.(?:4|72)|latest)(?![\w.])", "lmcad@v0.1.5", original)
            if updated != original:
                write_lf(cad, updated)
    # The checked-in bundle is a pinned staging input, not an optional SDK lookup.
    closure = args.stage / "docs/third-party-licenses/cycles"
    catalog = json.loads((closure / "manifest.json").read_text(encoding="utf-8"))
    for component in catalog["components"]:
        for entry in component["files"]:
            path = closure / entry["path"]
            path.resolve().relative_to(closure.resolve())
            if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
                raise SystemExit("licence hash mismatch: " + entry["path"])
    extra = json.loads((closure / "runtime-notices.json").read_text(encoding="utf-8"))
    for entry in extra["files"]:
        path = args.stage / "docs/third-party-licenses" / entry["file"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            raise SystemExit("runtime notice hash mismatch")
    def verify_notice_entries(value):
        if isinstance(value, dict):
            if isinstance(value.get("file"), str) and "sha256" in value:
                path = args.stage / "docs/third-party-licenses" / value["file"]
                path.resolve().relative_to((args.stage / "docs/third-party-licenses").resolve())
                if hashlib.sha256(path.read_bytes()).hexdigest() != value["sha256"]:
                    raise SystemExit("supplemental notice hash mismatch")
            for item in value.values(): verify_notice_entries(item)
        elif isinstance(value, list):
            for item in value: verify_notice_entries(item)
    for manifest in closure.glob("*.json"):
        verify_notice_entries(json.loads(manifest.read_text(encoding="utf-8")))
    for path in sorted(args.stage.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        if path.relative_to(args.stage).as_posix().startswith("docs/third-party-licenses/"):
            continue
        if path.suffix.lower() == ".json":
            try:
                original = json.loads(path.read_text(encoding="utf-8"))
            except ValueError:
                continue
            private[path.relative_to(args.stage).as_posix()] = original
            write_lf(path, json.dumps(scrub(original), indent=2) + "\n")
        elif path.suffix.lower() in TEXT:
            original = path.read_text(errors="replace")
            private[path.relative_to(args.stage).as_posix()] = original if PRIVATE.search(original) else None
            relative = path.relative_to(args.stage).as_posix()
            # Upstream licence/NOTICE texts are legal source material; preserve
            # their bytes exactly and let the public provenance scanner report
            # a genuine private marker instead of rewriting legal text.
            if relative.startswith("docs/third-party-licenses/"):
                continue
            if relative == "README.md":
                original = "\n".join(line for line in original.splitlines() if not line.startswith("Implementation source and its full development history")) + "\n"
            write_lf(path, safe_string(original))
    # Refresh nested file-integrity references transitively after source edits.
    previous = originals
    for _ in range(16):
        current = {p.relative_to(args.stage).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in args.stage.rglob("*") if p.is_file()}
        replacements = {old: current[name] for name, old in previous.items()
                        if name in current and old != current[name]}
        if not replacements and previous is not originals:
            break
        def refresh(v):
            if isinstance(v, str): return replacements.get(v, v)
            if isinstance(v, list): return [refresh(x) for x in v]
            if isinstance(v, dict):
                updated = {k: refresh(x) for k, x in v.items()}
                for path_key, hash_key in (("source", "sha256"), ("source", "source_sha256"),
                                           ("script", "script_sha256"), ("metadata", "sha256"),
                                           ("path", "sha256")):
                    name = updated.get(path_key)
                    if not isinstance(name, str) or hash_key not in updated:
                        continue
                    candidate = args.stage / name
                    try:
                        candidate.resolve().relative_to(args.stage.resolve())
                    except ValueError:
                        continue
                    if candidate.is_file():
                        updated[hash_key] = hashlib.sha256(candidate.read_bytes()).hexdigest()
                return updated
            return v
        previous = current
        for path in args.stage.rglob("*.json"):
            if path.relative_to(args.stage).as_posix().startswith("docs/third-party-licenses/"):
                continue
            original = json.loads(path.read_text(encoding="utf-8"))
            updated = refresh(original)
            if original != updated:
                write_lf(path, json.dumps(updated, indent=2) + "\n")
    else:
        raise SystemExit("cyclic content hash references")
    notices = args.stage / "THIRD_PARTY_NOTICES.md"
    if notices.exists():
        text = notices.read_text(encoding="utf-8")
        text = text.replace("Project-owned lmvideo source is licensed under the MIT License in [LICENSE](LICENSE).", "Example and documentation content is licensed under MIT in [LICENSE-examples](LICENSE-examples); binary releases use [LICENSE](LICENSE).")
        text = text.replace("the same terms as lmvideo itself", "the MIT terms identified by its own notice")
        text = text.replace("[scripts/skia.lock]", "the pinned public release manifest")
        text = text.replace("[scripts/fetch-skia.sh]", "the public release build records")
        text = text.replace("[scripts/sync-sdk.sh]", "the public release notices")
        text = text.replace("[sdk.lock]", "the public release runtime manifest")
        text = text.replace("docs/third-party-docs/third-party-licenses/", "docs/third-party-licenses/")
        write_lf(notices, text)
    sidecar = args.stage.parent / "public-safe-reviewed-private-map.json"
    write_lf(sidecar, json.dumps({
        "source": str(args.source.resolve()),
        "fields": private,
        "replacement_rules": {
            "lmctlhq/lmvideo": "reviewed-development-runtime",
            "/home/ or /tmp/ paths": "local-review/",
            "durable-memory/": "review-record:",
            "tests/": "witness:",
            "cpp/": "runtime-component:",
            "implementation/workdir/command/stdout fields": "reviewed-development-runtime",
        },
    }, indent=2) + "\n")
    subprocess.run([sys.executable, str(Path(__file__).with_name("check-public-provenance.py")), str(args.stage), "--report", str(args.stage.parent / (args.stage.name + "-scan.json"))], check=True)
    print(f"staged {args.stage} (private sidecar {sidecar})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
