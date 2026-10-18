#!/usr/bin/env bash
# Deterministic offline integration test runner for lmvideo.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

TMP_BUILD="$(mktemp -d -t lmvideo-build-XXXXXX)"
trap 'rm -rf "$TMP_BUILD"' EXIT

echo "=== Building in temporary directory: $TMP_BUILD ==="
nice make BUILD="$TMP_BUILD" all

echo "ok 1 - build completed in temporary directory"

echo "=== Running unit and golden tests ==="
"$TMP_BUILD/skia_test"
echo "ok 2 - skia_test passed"

echo "=== Running ffmpeg encoder tests ==="
"$TMP_BUILD/encoder_test"
echo "ok 2b - encoder_test passed"

echo "=== Running CLI demo command ==="
"$TMP_BUILD/lmvideo" demo "$TMP_BUILD/demo.png"
[ -f "$TMP_BUILD/demo.png" ]
python3 -I -c '
import struct, sys
path, exp_w, exp_h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(path, "rb") as f:
    sig = f.read(8)
    assert sig == b"\x89PNG\r\n\x1a\n", f"Invalid PNG signature in {path}"
    length, tag = struct.unpack(">I4s", f.read(8))
    assert tag == b"IHDR" and length == 13, f"Invalid IHDR chunk in {path}"
    w, h = struct.unpack(">II", f.read(8))
    assert (w, h) == (exp_w, exp_h), f"Dimensions mismatch: expected ({exp_w}, {exp_h}), got ({w}, {h})"
' "$TMP_BUILD/demo.png" 500 350
echo "ok 3 - lmvideo demo generated valid PNG (500x350)"

echo "=== Running CLI render command ==="
"$TMP_BUILD/lmvideo" render tests/golden/scene.svg "$TMP_BUILD/rendered_scene.png"
[ -f "$TMP_BUILD/rendered_scene.png" ]
python3 -I -c '
import struct, sys
path, exp_w, exp_h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(path, "rb") as f:
    sig = f.read(8)
    assert sig == b"\x89PNG\r\n\x1a\n", f"Invalid PNG signature in {path}"
    length, tag = struct.unpack(">I4s", f.read(8))
    assert tag == b"IHDR" and length == 13, f"Invalid IHDR chunk in {path}"
    w, h = struct.unpack(">II", f.read(8))
    assert (w, h) == (exp_w, exp_h), f"Dimensions mismatch: expected ({exp_w}, {exp_h}), got ({w}, {h})"
' "$TMP_BUILD/rendered_scene.png" 200 120
echo "ok 4 - lmvideo render generated valid PNG (200x120)"

echo "=== Testing CLI exit codes ==="
set +e
"$TMP_BUILD/lmvideo" >/dev/null 2>&1
status_no_args=$?
"$TMP_BUILD/lmvideo" render nonexistent.svg "$TMP_BUILD/out.png" >/dev/null 2>&1
status_runtime_err=$?
set -e

[ "$status_no_args" -eq 1 ]
echo "ok 5 - usage error exits with code 1"

[ "$status_runtime_err" -eq 3 ]
echo "ok 6 - runtime error exits with code 3"

echo "PASS all checks passed successfully"
