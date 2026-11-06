#!/usr/bin/env python3
"""Install and validate the pinned external FFmpeg build for Windows CI."""
import argparse, hashlib, json, os, shutil, subprocess, tempfile, urllib.request, zipfile
from pathlib import Path, PurePosixPath
ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "scripts/windows-ffmpeg.lock.json"
def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""): h.update(chunk)
    return h.hexdigest()
def safe_member(name):
    path = PurePosixPath(name.replace("\\", "/"))
    return not path.is_absolute() and ".." not in path.parts and ":" not in name and not name.startswith(("/", "\\"))
def extract_verified(archive, destination, lock):
    if archive.stat().st_size != lock["size"]: raise ValueError(f"FFmpeg archive size mismatch: {archive}")
    if digest(archive) != lock["sha256"]: raise ValueError(f"FFmpeg archive SHA-256 mismatch: {archive}")
    destination.mkdir(parents=True, exist_ok=False)
    with zipfile.ZipFile(archive) as zipped:
        members = zipped.infolist()
        if any(not safe_member(member.filename) for member in members): raise ValueError("FFmpeg archive contains an unsafe path")
        required = {item.replace("\\", "/") for item in lock["required"]}
        normalized = {item.filename.replace("\\", "/").split("/", 1)[-1] for item in members}
        if not required.issubset(normalized): raise ValueError("FFmpeg archive is missing required bin/ffmpeg.exe and/or bin/ffprobe.exe")
        for member in members:
            target = destination / member.filename
            if member.is_dir(): target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with zipped.open(member) as source, target.open("wb") as output: shutil.copyfileobj(source, output)
    roots = [item for item in destination.iterdir() if item.is_dir()]
    root = roots[0] if len(roots) == 1 and (roots[0] / "bin").is_dir() else destination
    ffmpeg, ffprobe = root / "bin" / "ffmpeg.exe", root / "bin" / "ffprobe.exe"
    if not ffmpeg.is_file() or not ffprobe.is_file(): raise ValueError("FFmpeg archive layout is not a single root/bin layout")
    return root, ffmpeg, ffprobe
def validate(ffmpeg, ffprobe):
    for executable in (ffmpeg, ffprobe):
        result = subprocess.run([str(executable), "-version"], capture_output=True, text=True, timeout=30, check=True)
        if not result.stdout.lower().startswith(executable.stem + " version "): raise ValueError(f"unexpected executable output from {executable}")
    encoders = subprocess.run([str(ffmpeg), "-hide_banner", "-encoders"], capture_output=True, text=True, timeout=30, check=True).stdout
    names = {parts[1] for parts in (line.split() for line in encoders.splitlines()) if len(parts) > 1}
    for name in ("libx264", "libvpx-vp9", "ffv1", "gif"):
        if name not in names: raise ValueError(f"pinned FFmpeg does not provide required encoder {name}")
def main():
    parser = argparse.ArgumentParser(); parser.add_argument("--archive", type=Path); parser.add_argument("--output-dir", type=Path, default=Path(os.environ.get("RUNNER_TEMP", tempfile.gettempdir())) / "lmvideo-ffmpeg"); parser.add_argument("--lock", type=Path, default=LOCK); args = parser.parse_args()
    lock = json.loads(args.lock.read_text(encoding="utf-8")); archive = args.archive
    if archive is None:
        archive = args.output_dir.parent / lock["asset"]; archive.parent.mkdir(parents=True, exist_ok=True); urllib.request.urlretrieve(lock["url"], archive)
    _, ffmpeg, ffprobe = extract_verified(archive.resolve(), args.output_dir.resolve(), lock); validate(ffmpeg, ffprobe)
    if os.environ.get("GITHUB_PATH"):
        with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8", newline="\n") as stream: stream.write(str(ffmpeg.parent) + "\n")
    print(f"FFmpeg ready: {ffmpeg}\nFFprobe ready: {ffprobe}\nFFmpeg external license: {lock['license']}")
if __name__ == "__main__": main()
