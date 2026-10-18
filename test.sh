#!/usr/bin/env bash
# End-to-end test of lmvideo, offline and deterministic. Needs g++ (or CXX), make, python3, ffmpeg + ffprobe and a mono build of
# lmscript: LMSCRIPT_BIN (default ../lmctl-mono/lmscript/bin/lmscript next to this checkout). `_dsl_` is not in the npm staging build.
# Skia is fetched once into third_party/ by scripts/fetch-skia.sh (pinned sha256; LMVIDEO_SKIA_ZIP=path reuses a local zip).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"
LMSCRIPT="${LMSCRIPT_BIN:-$HERE/../lmctl-mono/lmscript/bin/lmscript}"
[ -f "$LMSCRIPT" ] || { echo "FAIL: lmscript not found at $LMSCRIPT (set LMSCRIPT_BIN to a mono build)" >&2; exit 1; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lmvideo-test-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
pass=0; ok() { pass=$((pass + 1)); echo "ok $pass - $*"; }
SCHEMA="$HERE/schema/lmvideo.dsl.json"
PNGPX="python3 -I $HERE/tools/pngpx.py"
compile() { "$LMSCRIPT" "$@"; }
# near R G B TOL a b c d: is pixel "a b c d" within TOL of R G B?
within() { python3 -I -c 'import sys; a=list(map(int,sys.argv[1:4])); b=list(map(int,sys.argv[4:7])); t=int(sys.argv[7]); sys.exit(0 if all(abs(x-y)<=t for x,y in zip(a,b)) else 1)' "$@"; }
px() { $PNGPX "$1" "$2" "$3" | awk '{print $1, $2, $3}'; }   # prints "r g b" of one pixel
expect_px() { local file=$1 x=$2 y=$3 r=$4 g=$5 b=$6 tol=$7; within $(px "$file" "$x" "$y") $r $g $b $tol || fail "$file pixel ($x,$y) is $(px "$file" "$x" "$y"), expected ~ $r $g $b (tol $tol)"; }

# 0. The vendored runtime must be the pinned cpp-dsl-sdk header.
scripts/sync-sdk.sh --verify >/dev/null || fail "cpp/dslvm.hpp differs from sdk.lock (run scripts/sync-sdk.sh)"
ok "cpp/dslvm.hpp matches sdk.lock ($(sed -n 's/^commit=//p' sdk.lock | cut -c1-12))"

# 1. Build everything into a temp dir (nice: the box is shared).
nice make BUILD="$WORK/build" all >"$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; fail "build"; }
B="$WORK/build"
ok "library, CLI, tests and DSL host build"

# 2. Phase 1: library tests (pixels, text, PNG/JPEG round trips, SVG, golden) and CLI.
"$B/skia_test" >"$WORK/skia.out" 2>&1 || { tail -20 "$WORK/skia.out"; fail "skia_test"; }
ok "skia_test: $(tail -1 "$WORK/skia.out")"
"$B/lmvideo" demo "$WORK/demo.png" || fail "lmvideo demo"
[ "$($PNGPX "$WORK/demo.png")" = "500x350" ] || fail "demo.png size"
ok "lmvideo demo writes a valid 500x350 PNG"
"$B/lmvideo" render tests/golden/scene.svg "$WORK/scene.png" || fail "lmvideo render"
[ "$($PNGPX "$WORK/scene.png")" = "200x120" ] || fail "scene.png size"
ok "lmvideo render: SVG -> 200x120 PNG"
"$B/lmvideo" >/dev/null 2>&1; [ $? -eq 1 ] || fail "usage exit code"
"$B/lmvideo" render nonexistent.svg "$WORK/x.png" >/dev/null 2>&1; [ $? -eq 3 ] || fail "runtime error exit code"
ok "CLI exit codes: 1 usage, 3 runtime error"

# 3. Phase 2: ffmpeg encoder.
"$B/encoder_test" >"$WORK/enc.out" 2>&1 || { tail -20 "$WORK/enc.out"; fail "encoder_test"; }
ok "encoder_test: $(tail -1 "$WORK/enc.out")"

# 4. Phase 3: the DSL host. image.lms -> PNG.
HOST="$B/lmvideohost"
# Registry versus schema (spec v4 S0, A02): the host declares its signatures' types itself; they must equal the schema's.
if "$LMSCRIPT" --dsl-signatures "$SCHEMA" >"$WORK/schema.sigs" 2>/dev/null; then
  "$HOST" --dsl-signatures >"$WORK/host.sigs" || fail "host --dsl-signatures"
  diff -u "$WORK/schema.sigs" "$WORK/host.sigs" >"$WORK/sigs.diff" || { cat "$WORK/sigs.diff"; fail "registry signatures differ from the schema"; }
  ok "registry signatures equal the schema's ($(wc -l <"$WORK/host.sigs") entries, lmscript --dsl-signatures)"
else
  echo "skip - this lmscript has no --dsl-signatures (mono builds after 0.2.330 do); the host declares $("$HOST" --dsl-signatures | wc -l) signatures"
fi
# stdout is exactly one JSON result line (spec v4 S0, A03); out.print lines travel in result.output.
out_lines() { python3 -I -c 'import json,sys; d=json.load(sys.stdin); assert d["ok"] is True, d; print("\n".join(d["result"]["output"]))'; }
result_line() { # FILE EXPECTED_PREFIX: the file holds exactly one JSON line starting with the prefix
  [ "$(wc -l <"$1")" = 1 ] || fail "$1: stdout must be exactly one JSON line: $(cat "$1")"
  case "$(cat "$1")" in "$2"*) ;; *) fail "$1: result line $(cat "$1") does not start with $2";; esac
}
compile examples/image.lms "$WORK/image.dslb" "$WORK/image.png" 640 360 "Hello lmvideo" >/dev/null || fail "compile image.lms"
[ "$(od -An -tu1 -j4 -N2 "$WORK/image.dslb" | tr -s ' ' | sed 's/^ //')" = "0 3" ] || fail "bytecode version 3"
"$HOST" --dsl "$WORK/image.dslb" --schema "$SCHEMA" >"$WORK/image.out" 2>"$WORK/image.err" || { cat "$WORK/image.err"; fail "image host"; }
result_line "$WORK/image.out" '{"ok": true, "result": {"output": ["drew 640x360"], "file": "'"$WORK"'/image.png", "width": 640, "height": 360, "frames": 1, "fps": '
[ "$(out_lines <"$WORK/image.out")" = "drew 640x360" ] || fail "image.lms output: $(cat "$WORK/image.out")"
[ "$($PNGPX "$WORK/image.png")" = "640x360" ] || fail "image.png size"
expect_px "$WORK/image.png" 480 126 255 209 102 2    # sun: #ffd166 (cx 0.75w, cy 0.35h)
expect_px "$WORK/image.png" 5 5 30 60 114 4          # gradient start #1e3c72 (top-left corner)
expect_px "$WORK/image.png" 635 355 42 82 152 4      # gradient end #2a5298 (bottom-right corner)
expect_px "$WORK/image.png" 120 250 219 71 113 12    # translucent #ef476f over the gradient
ok "image.lms: 640x360 PNG with the sun, gradient and translucent card at the expected pixels"
mv "$WORK/image.png" "$WORK/image1.png"
"$HOST" --dsl "$WORK/image.dslb" --schema "$SCHEMA" >/dev/null 2>&1 || fail "second host run failed"
[ -f "$WORK/image.png" ] || fail "second run did not produce image.png"
[ "$($PNGPX "$WORK/image.png" --sha256)" = "$($PNGPX "$WORK/image1.png" --sha256)" ] || fail "two runs differ"
ok "image.lms: a second run gives identical pixels (deterministic, no system fonts)"
compile examples/image.lms "$WORK/small.dslb" "$WORK/small.png" 200 100 "Hi" >/dev/null || fail "compile small"
"$HOST" --dsl "$WORK/small.dslb" --schema "$SCHEMA" >/dev/null 2>&1 || fail "small host"
[ "$($PNGPX "$WORK/small.png")" = "200x100" ] || fail "\${} size reaches the host"
ok "image.lms: \${} literals (size 200x100, title, path) reach the host"

# 5. svg.lms: an SVG document drawn into the canvas.
compile examples/svg.lms "$WORK/svg.dslb" "$WORK/svg.png" tests/golden/scene.svg >/dev/null || fail "compile svg.lms"
"$HOST" --dsl "$WORK/svg.dslb" --schema "$SCHEMA" >"$WORK/svg.out" 2>/dev/null || fail "svg host"
[ "$(out_lines <"$WORK/svg.out")" = "svg is 200x120" ] || fail "svg output: $(cat "$WORK/svg.out")"
expect_px "$WORK/svg.png" 140 100 231 76 60 3        # the red #e74c3c rounded rect at (65..215, 60..153)
expect_px "$WORK/svg.png" 320 100 241 196 15 3       # the yellow #f1c40f circle
expect_px "$WORK/svg.png" 10 3 51 51 51 2            # the DSL's own bar on top
expect_px "$WORK/svg.png" 5 200 255 255 255 0        # white margin outside the SVG
ok "svg.lms: SVG fitted into a rect on a canvas the chunk also draws on"

# 6. video.lms -> mp4: the host owns the frame loop.
compile examples/video.lms "$WORK/video.dslb" "$WORK/video.mp4" 60 30 >/dev/null || fail "compile video.lms"
"$HOST" --dsl "$WORK/video.dslb" --schema "$SCHEMA" >/dev/null 2>"$WORK/video.err" || { cat "$WORK/video.err"; fail "video host"; }
INFO="$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,width,height,nb_read_frames:format=duration -of default=nw=1 "$WORK/video.mp4")"
echo "$INFO" | sed 's/^/    /'
[ "$(echo "$INFO" | tr '\n' ' ')" = "codec_name=h264 width=320 height=180 nb_read_frames=60 duration=2.000000 " ] || fail "ffprobe: $INFO"
ok "video.lms: mp4 is h264 320x180, 60 frames, 2.0 s"
frame() { ffmpeg -v error -y -i "$1" -vf "select=eq(n\,$2)" -frames:v 1 "$3"; }
frame "$WORK/video.mp4" 0 "$WORK/f0.png"; frame "$WORK/video.mp4" 59 "$WORK/f59.png"
expect_px "$WORK/f0.png" 40 108 255 209 102 40        # first frame: yellow ball at x 40, y 0.6h
expect_px "$WORK/f0.png" 300 20 11 19 43 25            # first frame background #0b132b
expect_px "$WORK/f59.png" 280 108 239 71 111 40        # last frame: ball at w-40, pink
expect_px "$WORK/f59.png" 300 20 91 192 190 25         # last frame background #5bc0be
ok "video.lms: frame 0 and frame 59 decode to the expected ball position and colours"

# 7. Compile-time rejection (exit 2, located error): wrong types, unknown names.
bad() { # NAME BODY EXPECTED_MESSAGE_PART
  printf 'p = _dsl_ schema="%s";\n%s\nend _dsl_\nsavedsl(p, "%s/%s.dslb");\n' "$SCHEMA" "$2" "$WORK" "$1" >"$WORK/$1.lms"
  "$LMSCRIPT" "$WORK/$1.lms" >"$WORK/$1.out" 2>&1; local code=$?
  [ "$code" = 2 ] && grep -q "$3" "$WORK/$1.out" || { cat "$WORK/$1.out"; fail "$1: expected exit 2 with '$3', got $code"; }
}
bad t1 'canvas.rect(1, 2, 3, 4, paint());' 'expects float'
bad t2 'canvas.circle(1.0, 2.0, 3.0, "red");' 'expects Paint'
bad t3 'canvas.sparkle(1.0);' "no method 'sparkle'"
bad t4 'let p = paint().color(5);' 'expects string'
bad t5 'video.size("a", 2);' 'expects int'
bad t6 'let x = 1.0 + 2;' 'no implicit conversion'
bad t7 'let f = sin(1);' 'expects float'
ok "seven ill-typed chunks are rejected at lmscript check time with located errors"

# 8. Host refusals before anything runs: schema hash mismatch, truncated file, wrong version.
sed 's/"name": "lmvideo"/"name": "lmvideo2"/' "$SCHEMA" >"$WORK/other.json"
"$HOST" --dsl "$WORK/image.dslb" --schema "$WORK/other.json" >"$WORK/e.out" 2>"$WORK/e.err"; [ $? = 3 ] && grep -q "schema mismatch" "$WORK/e.err" || fail "schema mismatch: $(cat "$WORK/e.err")"
result_line "$WORK/e.out" '{"ok": false, "phase": "load", "code": "LOAD_SCHEMA_MISMATCH", "message": "schema mismatch'
head -c 60 "$WORK/image.dslb" >"$WORK/trunc.dslb"
"$HOST" --dsl "$WORK/trunc.dslb" --schema "$SCHEMA" >"$WORK/e.out" 2>"$WORK/e.err"; [ $? = 3 ] && grep -q "truncated" "$WORK/e.err" || fail "truncated"
result_line "$WORK/e.out" '{"ok": false, "phase": "load", "code": "LOAD_BAD_BYTECODE", "message": "bytecode is truncated'
ok "host refuses a schema-hash mismatch and a truncated file (one JSON line, phase load, exit 3, nothing drawn)"

# 9. Run-time errors from the host: exit 3 with the method named; out.fail exit 4.
run_err() { # NAME EXPECTED_EXIT EXPECTED_MESSAGE_PART BODY
  printf 'p = _dsl_ schema="%s";\n%s\nend _dsl_\nsavedsl(p, "%s/%s.dslb");\n' "$SCHEMA" "$4" "$WORK" "$1" >"$WORK/$1.lms"
  "$LMSCRIPT" "$WORK/$1.lms" >"$WORK/$1.cout" 2>&1 || { cat "$WORK/$1.cout"; fail "$1 compile"; }
  "$HOST" --dsl "$WORK/$1.dslb" --schema "$SCHEMA" >"$WORK/$1.out" 2>"$WORK/$1.err"; local code=$?
  [ "$code" = "$2" ] && grep -q "$3" "$WORK/$1.err" || { cat "$WORK/$1.err"; fail "$1: expected exit $2 with '$3', got $code"; }
  if [ "$2" = 4 ]; then result_line "$WORK/$1.out" '{"ok": false, "phase": "app", "code": "APP_FAIL", "message": "'; else result_line "$WORK/$1.out" '{"ok": false, "phase": "run", "code": "RUN_HOST", "message": "'; fi
}
SETUP='video.size(64, 64); video.frames(1); video.output("'"$WORK"'/r.png");'
run_err r1 3 'Paint.color' "if (video.setup()) { $SETUP } else { canvas.rect(0.0, 0.0, 5.0, 5.0, paint().color(\"#12\")); }"
run_err r2 3 'not available during the setup pass' "canvas.clear(\"#ffffff\");"
run_err r3 3 'only be called in the setup pass' "if (video.setup()) { $SETUP } else { video.size(10, 10); }"
run_err r4 3 'video.output' "if (video.setup()) { video.size(64, 64); }"
run_err r5 3 'needs video.frames(1)' "if (video.setup()) { video.size(64, 64); video.frames(2); video.output(\"$WORK/r.png\"); }"
run_err r6 3 'size must be' "if (video.setup()) { video.size(63000, 64); }"
run_err r7 4 'fail: nope' "if (video.setup()) { $SETUP } else { out.fail(\"nope\"); }"
ok "host run-time errors: bad colour, drawing in setup, size in frame, missing output, image with 2 frames, bad size (phase run, RUN_HOST, exit 3); out.fail (phase app, APP_FAIL, exit 4)"

# 10. Defence in depth (reviewer finding): the VM verifier checks names and arities, not types, so a hand-built program with the
#     right schema hash could pass a Path where a Paint is expected. The host checks every argument against the schema.
printf 'p = _dsl_ schema="%s";\nif (video.setup()) { %s } else { canvas.rect(0.0, 0.0, 5.0, 5.0, paint()); }\nend _dsl_\nsavedsl(p, "%s/w.dslb");\n' "$SCHEMA" "$SETUP" "$WORK" >"$WORK/w.lms"
"$LMSCRIPT" "$WORK/w.lms" >/dev/null 2>&1 || fail "compile w.lms"
python3 -I - "$WORK/w.dslb" <<'PY' || fail "patch bytecode"
import struct, sys
d = bytearray(open(sys.argv[1], "rb").read())
pos = 4 + 2 + 32
n, = struct.unpack(">H", d[pos:pos + 2]); pos += 2
for _ in range(n):                       # rename the constant "paint" to "path": a verified program that returns a Path for a Paint
    ln, = struct.unpack(">H", d[pos:pos + 2])
    if d[pos + 2:pos + 2 + ln] == b"paint":
        d[pos:pos + 2 + ln] = struct.pack(">H", 4) + b"path"; break
    pos += 2 + ln
else: raise SystemExit("constant not found")
open(sys.argv[1], "wb").write(d)
PY
"$HOST" --dsl "$WORK/w.dslb" --schema "$SCHEMA" >/dev/null 2>"$WORK/w.err"; code=$?
[ "$code" = 3 ] && grep -q 'Canvas.rect: argument 5 must be Paint, got Path' "$WORK/w.err" || { cat "$WORK/w.err"; fail "wrong-type bytecode: exit $code"; }
ok "hand-built bytecode passing a Path as a Paint is stopped by the host's schema type check (exit 3, no crash)"
sed 's|"fill": {"params": \[\], "returns": "Paint"},|&\n        "extra": {"params": [], "returns": "Paint"},|' "$SCHEMA" >"$WORK/extra.json"
grep -q '"extra"' "$WORK/extra.json" || fail "schema edit"
"$HOST" --dsl "$WORK/image.dslb" --schema "$WORK/extra.json" >"$WORK/e.out" 2>"$WORK/e.err"; code=$?
[ "$code" = 3 ] && grep -q 'missing method Paint.extra' "$WORK/e.err" || { cat "$WORK/e.err"; fail "registry self-check: exit $code"; }
result_line "$WORK/e.out" '{"ok": false, "phase": "load", "code": "LOAD_UNBOUND", "message": "host registry does not match the schema: missing method Paint.extra;'
sed 's|"fill": {"params": \[\], "returns": "Paint"},|"fill": {"params": ["int"], "returns": "Paint"},|' "$SCHEMA" >"$WORK/typed.json"
"$HOST" --dsl "$WORK/image.dslb" --schema "$WORK/typed.json" >"$WORK/e.out" 2>"$WORK/e.err"; code=$?
[ "$code" = 3 ] && grep -q 'signature of Paint.fill' "$WORK/e.err" || { cat "$WORK/e.err"; fail "registry type self-check: exit $code"; }
ok "the host refuses to start when the schema has a method its registry lacks or declares with other types (registry/schema self-check, LOAD_UNBOUND, exit 3)"
run_err r8 3 'interpolation factor must be finite' "if (video.setup()) { $SETUP } else { canvas.clear(mix(\"#000000\", \"#ffffff\", 0.0 / 0.0)); }"
run_err r9 3 'cannot write' "if (video.setup()) { video.size(8, 8); video.frames(1); video.output(\"$WORK/no/such/dir/x.jpg\"); } else { canvas.clear(\"#ffffff\"); }"
rm -f "$WORK/fail.mp4"
run_err r10 4 'fail: stop' "if (video.setup()) { video.size(64, 64); video.frames(5); video.output(\"$WORK/fail.mp4\"); } else { if (video.index() == 1) { out.fail(\"stop\"); } }"
[ ! -e "$WORK/fail.mp4" ] && [ -z "$(ls -A "$WORK" | grep '^\.lmv-')" ] || fail "a failed video run left a partial file"
ok "NaN colour factor and an unwritable image path are errors (exit 3); out.fail during a video leaves no partial file"

echo "PASS $pass checks"
