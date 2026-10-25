#!/usr/bin/env bash
# End-to-end test of lmvideo, offline and deterministic. Needs g++ (or CXX), make, python3, ffmpeg + ffprobe and lmscript >= 0.2.346.
# Set LMSCRIPT_BIN to override the lmscript executable; otherwise the released command is resolved from PATH.
# Skia is fetched once into third_party/ by scripts/fetch-skia.sh (pinned sha256; LMVIDEO_SKIA_ZIP=path reuses a local zip).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"
LMSCRIPT="${LMSCRIPT_BIN:-$(command -v lmscript || true)}"
[ -n "$LMSCRIPT" ] && [ -x "$LMSCRIPT" ] || { echo "FAIL: lmscript >= 0.2.346 not found in PATH (or set LMSCRIPT_BIN)" >&2; exit 1; }
LMSCRIPT_VERSION="$($LMSCRIPT --version 2>/dev/null | sed -nE 's/.*([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' | head -1)"
[ -n "$LMSCRIPT_VERSION" ] || { echo "FAIL: cannot read lmscript version from $LMSCRIPT" >&2; exit 1; }
[ "$(printf '%s\n' 0.2.346 "$LMSCRIPT_VERSION" | sort -V | head -1)" = "0.2.346" ] || { echo "FAIL: lmscript $LMSCRIPT_VERSION is older than 0.2.346" >&2; exit 1; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lmvideo-test-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
pass=0; ok() { pass=$((pass + 1)); echo "ok $pass - $*"; }
SCHEMA="$HERE/schema/lmvideo.dsl.json"
PNGPX="python3 -I $HERE/tools/pngpx.py"
compile() {
  local source="${1:-}"
  if [ -f "$source" ] && grep -Eq '([[:alnum:]_]+\.)?(plot|panel)\(|document\(|pdf_document\(' "$source"; then
    local local_schema="$WORK/$(basename "$source" .lms)-local-schema.lms"
    sed -E "s#program = _dsl_ cli=\"lmvideo\" install=\"github:lmctl-ai/lmvideo@[^\"]+\";#program = _dsl_ schema=\"$SCHEMA\";#" "$source" >"$local_schema"
    shift
    "$LMSCRIPT" "$local_schema" "$@"
  else
    "$LMSCRIPT" "$@"
  fi
}
# near R G B TOL a b c d: is pixel "a b c d" within TOL of R G B?
within() { python3 -I -c 'import sys; a=list(map(int,sys.argv[1:4])); b=list(map(int,sys.argv[4:7])); t=int(sys.argv[7]); sys.exit(0 if all(abs(x-y)<=t for x,y in zip(a,b)) else 1)' "$@"; }
px() { $PNGPX "$1" "$2" "$3" | awk '{print $1, $2, $3}'; }   # prints "r g b" of one pixel
expect_px() { local file=$1 x=$2 y=$3 r=$4 g=$5 b=$6 tol=$7; within $(px "$file" "$x" "$y") $r $g $b $tol || fail "$file pixel ($x,$y) is $(px "$file" "$x" "$y"), expected ~ $r $g $b (tol $tol)"; }

# 0. The vendored runtime must be the pinned lmcode-cpp header.
scripts/sync-sdk.sh --verify >/dev/null || fail "cpp/dslvm.hpp differs from sdk.lock (run scripts/sync-sdk.sh)"
ok "cpp/dslvm.hpp matches sdk.lock ($(sed -n 's/^commit=//p' sdk.lock | cut -c1-12))"

# 1. Build everything into a temp dir (nice: the box is shared).
nice make BUILD="$WORK/build" all >"$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; fail "build"; }
B="$WORK/build"
ok "library, CLI, tests and DSL host build"

# 2. Phase 1: library tests (pixels, text, PNG/JPEG round trips, SVG, golden) and CLI.
"$B/skia_test" >"$WORK/skia.out" 2>&1 || { grep -a "^not ok" "$WORK/skia.out"; tail -5 "$WORK/skia.out"; fail "skia_test"; }
ok "skia_test: $(tail -1 "$WORK/skia.out")"
"$B/vector_test" >"$WORK/vector.out" 2>&1 || { cat "$WORK/vector.out"; fail "vector_test"; }
ok "vector_test: $(tail -1 "$WORK/vector.out")"
"$B/plot_test" >"$WORK/plot.out" 2>&1 || { cat "$WORK/plot.out"; fail "plot_test"; }
ok "plot_test: $(tail -1 "$WORK/plot.out")"
"$B/layout_test" >"$WORK/layout.out" 2>&1 || { cat "$WORK/layout.out"; fail "layout_test"; }
ok "layout_test: $(tail -1 "$WORK/layout.out")"
"$B/lmvideo" demo "$WORK/demo.png" || fail "lmvideo demo"
[ "$($PNGPX "$WORK/demo.png")" = "500x350" ] || fail "demo.png size"
ok "lmvideo demo writes a valid 500x350 PNG"
"$B/lmvideo" render tests/golden/scene.svg "$WORK/scene.png" || fail "lmvideo render"
[ "$($PNGPX "$WORK/scene.png")" = "200x120" ] || fail "scene.png size"
ok "lmvideo render: SVG -> 200x120 PNG"
"$B/lmvideo" >/dev/null 2>&1; [ $? -eq 2 ] || fail "usage exit code"
[ "$("$B/lmvideo" --version)" = "lmvideo $(head -n1 VERSION)" ] || fail "--version"
"$B/lmvideo" render nonexistent.svg "$WORK/x.png" >/dev/null 2>&1; [ $? -eq 3 ] || fail "runtime error exit code"
ok "CLI exit codes: 1 usage, 3 runtime error"

# 3. Phase 2: ffmpeg encoder.
"$B/encoder_test" >"$WORK/enc.out" 2>&1 || { tail -20 "$WORK/enc.out"; fail "encoder_test"; }
ok "encoder_test: $(tail -1 "$WORK/enc.out")"

# 4. Phase 3: the DSL host. image.lms -> PNG.
HOST="$B/lmvideo"
export PATH="$B:$PATH"   # examples declare cli="lmvideo": lmscript asks the freshly built host for its schema
# Registry versus schema: the host declares its signatures' types; they must equal the schema's.
if "$LMSCRIPT" --dsl-signatures "$SCHEMA" >"$WORK/schema.sigs" 2>/dev/null; then
  "$HOST" --dsl-signatures >"$WORK/host.sigs" || fail "host --dsl-signatures"
  diff -u "$WORK/schema.sigs" "$WORK/host.sigs" >"$WORK/sigs.diff" || { cat "$WORK/sigs.diff"; fail "registry signatures differ from the schema"; }
  ok "registry signatures equal the schema's ($(wc -l <"$WORK/host.sigs" | tr -d " ") entries, lmscript --dsl-signatures)"
else
  echo "skip - this lmscript has no --dsl-signatures; the host declares $("$HOST" --dsl-signatures | wc -l) signatures"
fi
# DSL host flag standard: the schema is embedded; --dsl-schema --json prints its exact bytes, --dsl-schema the shared summary.
"$HOST" --dsl-schema --json >"$WORK/embedded.json" || fail "--dsl-schema --json"
cmp -s "$SCHEMA" "$WORK/embedded.json" || fail "--dsl-schema --json differs from schema/lmvideo.dsl.json"
ok "--dsl-schema --json prints the embedded schema byte for byte"
python3 -I tests/schema-cli.py "$HOST" "$SCHEMA" >"$WORK/schema-cli.out" 2>&1 || { cat "$WORK/schema-cli.out"; fail "--dsl-schema summary format / flag combinations"; }
ok "--dsl-schema summary uses the shared format; other flag combinations exit 2 with empty stdout"
out="$("$HOST" --dsl "$WORK/none.dslb" --schema "$SCHEMA" 2>/dev/null)"; code=$?
[ "$code" = 2 ] && [ -z "$out" ] || fail "removed --schema FILE accepted (exit $code): $out"
ok "removed --schema FILE option refused (exit 2, nothing on stdout)"
# stdout is exactly one JSON result line; out.print lines travel in result.output.
out_lines() { python3 -I -c 'import json,sys; d=json.load(sys.stdin); assert d["ok"] is True, d; print("\n".join(d["result"]["output"]))'; }
result_line() { # FILE EXPECTED_PREFIX: the file holds exactly one JSON line starting with the prefix
  [ "$(wc -l <"$1" | tr -d " ")" = 1 ] || fail "$1: stdout must be exactly one JSON line: $(cat "$1")"
  case "$(cat "$1")" in "$2"*) ;; *) fail "$1: result line $(cat "$1") does not start with $2";; esac
}
compile examples/image.lms "$WORK/image.dslb" "$WORK/image.png" 640 360 "Hello lmvideo" >/dev/null || fail "compile image.lms"
BCV="$(python3 -I -c 'import sys; b=open(sys.argv[1],"rb").read(6); print(b[4], b[5])' "$WORK/image.dslb")"
[ "$BCV" = "0 5" ] || fail "bytecode version 5 (header bytes 4-5 are: $BCV)"
"$HOST" --dsl "$WORK/image.dslb" >"$WORK/image.out" 2>"$WORK/image.err" || { cat "$WORK/image.err"; fail "image host"; }
result_line "$WORK/image.out" '{"ok": true, "result": {"output": ["drew 640x360"], "file": "'"$WORK"'/image.png", "width": 640, "height": 360, "frames": 1, "fps": '
[ "$(out_lines <"$WORK/image.out")" = "drew 640x360" ] || fail "image.lms output: $(cat "$WORK/image.out")"
[ "$($PNGPX "$WORK/image.png")" = "640x360" ] || fail "image.png size"
expect_px "$WORK/image.png" 480 126 255 209 102 2    # sun: #ffd166 (cx 0.75w, cy 0.35h)
expect_px "$WORK/image.png" 5 5 30 60 114 4          # gradient start #1e3c72 (top-left corner)
expect_px "$WORK/image.png" 635 355 42 82 152 4      # gradient end #2a5298 (bottom-right corner)
expect_px "$WORK/image.png" 120 250 219 71 113 12    # translucent #ef476f over the gradient
ok "image.lms: 640x360 PNG with the sun, gradient and translucent card at the expected pixels"
mv "$WORK/image.png" "$WORK/image1.png"
"$HOST" --dsl "$WORK/image.dslb" >/dev/null 2>&1 || fail "second host run failed"
[ -f "$WORK/image.png" ] || fail "second run did not produce image.png"
[ "$($PNGPX "$WORK/image.png" --sha256)" = "$($PNGPX "$WORK/image1.png" --sha256)" ] || fail "two runs differ"
ok "image.lms: a second run gives identical pixels (deterministic, no system fonts)"
compile examples/image.lms "$WORK/small.dslb" "$WORK/small.png" 200 100 "Hi" >/dev/null || fail "compile small"
"$HOST" --dsl "$WORK/small.dslb" >/dev/null 2>&1 || fail "small host"
[ "$($PNGPX "$WORK/small.png")" = "200x100" ] || fail "\${} size reaches the host"
ok "image.lms: \${} literals (size 200x100, title, path) reach the host"

# 5. svg.lms: an SVG document drawn into the canvas.
compile examples/svg.lms "$WORK/svg.dslb" "$WORK/svg.png" tests/golden/scene.svg >/dev/null || fail "compile svg.lms"
"$HOST" --dsl "$WORK/svg.dslb" >"$WORK/svg.out" 2>/dev/null || fail "svg host"
[ "$(out_lines <"$WORK/svg.out")" = "svg is 200x120" ] || fail "svg output: $(cat "$WORK/svg.out")"
expect_px "$WORK/svg.png" 140 100 231 76 60 3        # the red #e74c3c rounded rect at (65..215, 60..153)
expect_px "$WORK/svg.png" 320 100 241 196 15 3       # the yellow #f1c40f circle
expect_px "$WORK/svg.png" 10 3 51 51 51 2            # the DSL's own bar on top
expect_px "$WORK/svg.png" 5 200 255 255 255 0        # white margin outside the SVG
ok "svg.lms: SVG fitted into a rect on a canvas the chunk also draws on"

# 5b. terminal-shot.lms: terminal text rendered into a 1000px window PNG.
printf '$ date -u\nSat Oct 24 15:00:00 UTC 2026\n$ echo "hello"\nhello\n' >"$WORK/term.txt"
compile examples/terminal-shot.lms "$WORK/term.txt" "$WORK/term.png" "bash - 80x24" "$WORK/term.dslb" >/dev/null || fail "compile terminal-shot.lms"
[ -f "$WORK/term.png" ] || "$HOST" --dsl "$WORK/term.dslb" >/dev/null 2>&1 || fail "run terminal-shot"
[ "$(python3 -I "$HERE/tools/pngpx.py" "$WORK/term.png" | cut -dx -f1)" = "1000" ] || fail "terminal-shot width != 1000"
expect_px "$WORK/term.png" 5 5 13 17 23 2              # outer background #0d1117
expect_px "$WORK/term.png" 44 43 255 95 86 5           # red dot #ff5f56 (cx 44, cy 43)
expect_px "$WORK/term.png" 62 43 255 189 46 5          # yellow dot #ffbd2e (cx 62, cy 43)
expect_px "$WORK/term.png" 80 43 39 201 63 5           # green dot #27c93f (cx 80, cy 43)
expect_px "$WORK/term.png" 100 100 22 27 34 2         # window background #161b22
mv "$WORK/term.png" "$WORK/term1.png"
"$HOST" --dsl "$WORK/term.dslb" >/dev/null 2>&1 || fail "second terminal host run"
[ -f "$WORK/term.png" ] || fail "second run did not produce term.png"
[ "$($PNGPX "$WORK/term.png" --sha256)" = "$($PNGPX "$WORK/term1.png" --sha256)" ] || fail "terminal-shot is not deterministic"
ok "terminal-shot.lms: 1000px PNG with title bar dots, dark card and deterministic rerun"

# 6. video.lms -> mp4: the DSL owns the frame loop; the host owns encoding.
compile examples/video.lms "$WORK/video.dslb" "$WORK/video.mp4" 60 30 >/dev/null || fail "compile video.lms"
"$HOST" --dsl "$WORK/video.dslb" >/dev/null 2>"$WORK/video.err" || { cat "$WORK/video.err"; fail "video host"; }
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

# 6b. template.lms -> mp4 and a PNG snapshot in one DSL invocation. The same
# host collection is walked on every frame, and the final frame is saved as PNG.
compile examples/template.lms "$WORK/template.dslb" "$WORK/template.mp4" "$WORK/template.png" >/dev/null || fail "compile template.lms"
"$HOST" --dsl "$WORK/template.dslb" >"$WORK/template.out" 2>"$WORK/template.err" || { cat "$WORK/template.err"; fail "template host"; }
result_line "$WORK/template.out" '{"ok": true, "result": {"output": ['
TINFO="$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,width,height,nb_read_frames:format=duration -of default=nw=1 "$WORK/template.mp4")"
[ "$(echo "$TINFO" | tr '\n' ' ')" = "codec_name=h264 width=320 height=180 nb_read_frames=12 duration=1.000000 " ] || fail "template ffprobe: $TINFO"
[ "$($PNGPX "$WORK/template.png")" = "320x180" ] || fail "template PNG size"
frame "$WORK/template.mp4" 0 "$WORK/t0.png"; frame "$WORK/template.mp4" 11 "$WORK/t11.png"
expect_px "$WORK/t0.png" 40 90 255 209 102 45
expect_px "$WORK/t11.png" 280 90 239 71 111 45
expect_px "$WORK/template.png" 40 145 255 209 102 0
expect_px "$WORK/template.png" 160 145 6 214 160 0
expect_px "$WORK/template.png" 280 145 239 71 111 0
frame "$WORK/template.mp4" 5 "$WORK/t5.png"
expect_px "$WORK/t5.png" 144 90 248 149 106 40
ffmpeg -v error -i "$WORK/template.mp4" -f framemd5 "$WORK/template1.md5"
cp "$WORK/template.png" "$WORK/template1.png"
"$HOST" --dsl "$WORK/template.dslb" >/dev/null 2>&1 || fail "second template host run"
[ "$($PNGPX "$WORK/template.png" --sha256)" = "$($PNGPX "$WORK/template1.png" --sha256)" ] || fail "template PNG is not deterministic"
ffmpeg -v error -i "$WORK/template.mp4" -f framemd5 "$WORK/template2.md5"
cmp "$WORK/template1.md5" "$WORK/template2.md5" || fail "template decoded frames differ on rerun"
python3 -I - "$WORK/template.out" "$WORK/template.dslb" <<'PY_METRICS' || fail "template metrics"
import json,sys,os
r=json.load(open(sys.argv[1]))['result']
assert r['frames']==12 and r['bytecode_bytes']==os.path.getsize(sys.argv[2])
assert r['instructions']>1000 and r['wall_ms']>=0
print('S4 template measurement:',json.dumps(r,sort_keys=True))
PY_METRICS
grep -q 'bytecode_bytes' "$WORK/template.out" || fail "template metrics missing"
grep -q 'instructions' "$WORK/template.out" || fail "template instruction metric missing"
ok "template.lms: 12-frame mp4, final PNG, len()/at() template loop, and deterministic rerun"

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
bad t7 'let f = sin(1);' 'expects (float) -> float'
ok "seven ill-typed chunks are rejected at lmscript check time with located errors"

# 8. Host refusals before anything runs: schema hash mismatch, truncated file, wrong version.
sed 's/"name": "lmvideo"/"name": "lmvideo2"/' "$SCHEMA" >"$WORK/other.json"
printf 'p = _dsl_ schema="%s";\nout.print("x");\nend _dsl_\nsavedsl(p, "%s/other.dslb");\n' "$WORK/other.json" "$WORK" >"$WORK/other.lms"
"$LMSCRIPT" "$WORK/other.lms" >/dev/null 2>&1 || fail "compile against the other schema"
"$HOST" --dsl "$WORK/other.dslb" >"$WORK/e.out" 2>"$WORK/e.err"; [ $? = 3 ] && grep -q "schema mismatch" "$WORK/e.err" || fail "schema mismatch: $(cat "$WORK/e.err")"
result_line "$WORK/e.out" '{"ok": false, "phase": "load", "code": "LOAD_SCHEMA_MISMATCH", "message": "schema mismatch'
head -c 60 "$WORK/image.dslb" >"$WORK/trunc.dslb"
"$HOST" --dsl "$WORK/trunc.dslb" >"$WORK/e.out" 2>"$WORK/e.err"; [ $? = 3 ] && grep -q "truncated" "$WORK/e.err" || fail "truncated"
result_line "$WORK/e.out" '{"ok": false, "phase": "load", "code": "LOAD_BAD_BYTECODE", "message": "bytecode is truncated'
ok "host refuses a schema-hash mismatch and a truncated file (one JSON line, phase load, exit 3, nothing drawn)"

# 9. Run-time errors from the host: exit 3 with the method named; out.fail exit 4.
run_err() { # NAME EXPECTED_EXIT EXPECTED_MESSAGE_PART BODY
  printf 'p = _dsl_ schema="%s";\n%s\nend _dsl_\nsavedsl(p, "%s/%s.dslb");\n' "$SCHEMA" "$4" "$WORK" "$1" >"$WORK/$1.lms"
  "$LMSCRIPT" "$WORK/$1.lms" >"$WORK/$1.cout" 2>&1 || { cat "$WORK/$1.cout"; fail "$1 compile"; }
  "$HOST" --dsl "$WORK/$1.dslb" >"$WORK/$1.out" 2>"$WORK/$1.err"; local code=$?
  [ "$code" = "$2" ] && grep -q "$3" "$WORK/$1.err" || { cat "$WORK/$1.err"; fail "$1: expected exit $2 with '$3', got $code"; }
  if [ "$2" = 4 ]; then result_line "$WORK/$1.out" '{"ok": false, "phase": "app", "code": "APP_FAIL", "message": "'; else result_line "$WORK/$1.out" '{"ok": false, "phase": "run", "code": "RUN_HOST", "message": "'; fi
}
SETUP='video.size(64, 64); video.frames(1); video.output("'"$WORK"'/r.png");'
run_err r1 3 'Paint.color' "$SETUP video.begin(); video.frame(); canvas.rect(0.0, 0.0, 5.0, 5.0, paint().color(\"#12\")); video.end();"
run_err r2 3 'before video.begin' 'canvas.clear("#ffffff");'
run_err r3 3 'before video.begin' "$SETUP video.begin(); video.size(10, 10);"
run_err r4 3 'video.output' 'video.size(64, 64); video.begin();'
run_err r5 3 'needs video.frames(1)' 'video.size(64, 64); video.frames(2); video.output("'"$WORK"'/r.png"); video.begin();'
run_err r6 3 'size must be' 'video.size(63000, 64); video.frames(1); video.output("'"$WORK"'/r.png"); video.begin();'
python3 - "$WORK/r6.out" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["phase"] == "run" and d["code"] == "RUN_HOST", d
assert d["line"] == 2 and d["column"] == 7, d
assert d["message"].startswith("dsl line 2:7:") and "size must be" in d["message"], d
PY
run_err r7 4 'fail: nope' "$SETUP video.begin(); video.frame(); out.fail(\"nope\"); video.end();"
python3 - "$WORK/r7.out" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["phase"] == "app" and d["code"] == "APP_FAIL", d
assert d["line"] == 2 and d["column"] > 0, d
assert d["message"].startswith("dsl line 2:") and "nope" in d["message"], d
PY
ok "host lifecycle errors: bad colour, drawing before begin, configuration after begin, missing output, image with 2 frames, bad size (exit 3); out.fail (exit 4)"

# 10. Defence in depth (reviewer finding): the VM verifier checks names and arities, not types, so a hand-built program with the
#     right schema hash could pass a Path where a Paint is expected. The host checks every argument against the schema.
printf 'p = _dsl_ schema="%s";\n%s video.begin(); video.frame(); canvas.rect(0.0, 0.0, 5.0, 5.0, paint()); video.end();\nend _dsl_\nsavedsl(p, "%s/w.dslb");\n' "$SCHEMA" "$SETUP" "$WORK" >"$WORK/w.lms"
"$LMSCRIPT" "$WORK/w.lms" >/dev/null 2>&1 || fail "compile w.lms"
python3 -I - "$WORK/w.dslb" <<'PY' || fail "patch bytecode"
import struct, sys
d = bytearray(open(sys.argv[1], "rb").read())
pos = 4 + 2 + 32                         # bytecode v5: u32 constant count, each u8 tag + u32 length + UTF-8
n, = struct.unpack(">I", d[pos:pos + 4]); pos += 4
for _ in range(n):                       # rewrite the import "fn paint() -> Paint" to "fn path() -> Path": a verified program that returns a Path for a Paint
    ln, = struct.unpack(">I", d[pos + 1:pos + 5])
    if d[pos + 5:pos + 5 + ln] == b"fn paint() -> Paint":
        d[pos + 1:pos + 5 + ln] = struct.pack(">I", 17) + b"fn path() -> Path"; break
    pos += 5 + ln
else: raise SystemExit("constant not found")
open(sys.argv[1], "wb").write(d)
PY
"$HOST" --dsl "$WORK/w.dslb" >/dev/null 2>"$WORK/w.err"; code=$?
[ "$code" = 3 ] && grep -q 'Canvas.rect: argument 5 must be Paint, got Path' "$WORK/w.err" || { cat "$WORK/w.err"; fail "wrong-type bytecode: exit $code"; }
ok "hand-built bytecode passing a Path as a Paint is stopped by the host's schema type check (exit 3, no crash)"
python3 - "$SCHEMA" "$WORK/extra.json" <<'PY'
import json, sys
src, dst = sys.argv[1:]
data = json.load(open(src))
data["types"]["Paint"]["methods"]["extra"] = [{"params": [], "returns": "self"}]
json.dump(data, open(dst, "w"), indent=2)
open(dst, "a").write("\n")
PY
grep -q '"extra"' "$WORK/extra.json" || fail "schema edit"
variant_host() { # NAME SCHEMA: a host embedding an edited schema, linked against the already built library
  cp -Rp "$B" "$WORK/build-$1" && rm -rf "$WORK/build-$1/host.o" "$WORK/build-$1/lmvideo" "$WORK/build-$1/generated" || fail "copy build for $1"
  nice make BUILD="$WORK/build-$1" LMVIDEO_SCHEMA="$2" "$WORK/build-$1/lmvideo" >"$WORK/make-$1.log" 2>&1 || { tail -30 "$WORK/make-$1.log"; fail "build $1 host"; }
}
variant_host extra "$WORK/extra.json"
"$WORK/build-extra/lmvideo" --dsl "$WORK/image.dslb" >"$WORK/e.out" 2>"$WORK/e.err"; code=$?
[ "$code" = 3 ] && grep -q 'missing method Paint.extra' "$WORK/e.err" || { cat "$WORK/e.err"; fail "registry self-check: exit $code"; }
result_line "$WORK/e.out" '{"ok": false, "phase": "load", "code": "LOAD_UNBOUND", "message": "host registry does not match the schema: missing method Paint.extra;'
python3 - "$SCHEMA" "$WORK/typed.json" <<'PY'
import json, sys
src, dst = sys.argv[1:]
data = json.load(open(src))
data["types"]["Paint"]["methods"]["fill"][0]["params"] = ["int"]
json.dump(data, open(dst, "w"), indent=2)
open(dst, "a").write("\n")
PY
variant_host typed "$WORK/typed.json"
"$WORK/build-typed/lmvideo" --dsl "$WORK/image.dslb" >"$WORK/e.out" 2>"$WORK/e.err"; code=$?
[ "$code" = 3 ] && grep -q 'signature of Paint.fill' "$WORK/e.err" || { cat "$WORK/e.err"; fail "registry type self-check: exit $code"; }
ok "a host built with an edited embedded schema refuses to start when the schema has a method its registry lacks or declares with other types (registry/schema self-check, LOAD_UNBOUND, exit 3)"
run_err r8 3 'interpolation factor must be finite' "$SETUP video.begin(); video.frame(); canvas.clear(mix(\"#000000\", \"#ffffff\", 0.0 / 0.0)); video.end();"
run_err r9 3 'cannot write' "video.size(8, 8); video.frames(1); video.output(\"$WORK/no/such/dir/x.jpg\"); video.begin(); video.frame(); canvas.clear(\"#ffffff\"); video.end();"
rm -f "$WORK/fail.mp4"
run_err r10 4 'fail: stop' "video.size(64, 64); video.frames(5); video.output(\"$WORK/fail.mp4\"); video.begin(); for (i in range(0, video.count())) { video.frame(); if (video.index() == 1) { out.fail(\"stop\"); } } video.end();"
[ ! -e "$WORK/fail.mp4" ] && [ -z "$(ls -A "$WORK" | grep '^\.lmv-')" ] || fail "a failed video run left a partial file"
ok "NaN colour factor and an unwritable image path are errors (exit 3); out.fail during a video leaves no partial file"

# 12. VM failures retain the DSL source location; the computed zero bypasses the compiler's literal-zero rejection.
printf 'p = _dsl_ schema="%s";\nlet z = 1 - 1;\nlet x = 1 / z;\nend _dsl_\nsavedsl(p, "%s/located.dslb");\n' "$SCHEMA" "$WORK" >"$WORK/located.lms"
compile "$WORK/located.lms" >/dev/null 2>&1 || fail "compile located division"
"$HOST" --dsl "$WORK/located.dslb" >"$WORK/located.out" 2>"$WORK/located.err"; code=$?
[ "$code" = 3 ] || fail "located division exit $code: $(cat "$WORK/located.out")"
python3 - "$WORK/located.out" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["phase"] == "run" and d["code"] == "RUN_ERROR", d
assert d["line"] == 3 and d["column"] == 11, d
assert d["message"] == "dsl line 3:11: int division by zero", d
PY
ok "computed division by zero reports DSL line 3:11"

run_err r11 3 'active video.begin' 'video.frame();'
run_err r12 3 'exactly video.frames' "$SETUP video.begin(); video.end();"
run_err r13 3 'may only be called once' "$SETUP video.begin(); video.begin();"
run_err r14 3 'more times than video.frames' "$SETUP video.begin(); video.frame(); video.frame();"
ok "host rejects frame without begin, missing frames, repeated begin and extra frames"

# 13. Animated plot examples and the remaining lmdraw ports: 960x540 h264 plus a PNG still.
for n in damped-oscillator kepler-second-law lotka-volterra hohmann-transfer carnot-cycle lens-ray-diagram rc-lowpass hydrogen-spectrum geometry-ellipse relativity-lorentz quarterly-revenue sensor-calibration forecast-area waveform-sampling; do
  compile examples/$n.lms "$WORK/$n.dslb" "$WORK/$n.mp4" "$WORK/$n.png" >/dev/null || fail "compile $n.lms"
  "$HOST" --dsl "$WORK/$n.dslb" >/dev/null 2>"$WORK/$n.err" || { cat "$WORK/$n.err"; fail "run $n.lms"; }
  INFO="$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,width,height,nb_read_frames -of default=nw=1 "$WORK/$n.mp4" | tr '\n' ' ')"
  [ "$INFO" = "codec_name=h264 width=960 height=540 nb_read_frames=240 " ] || fail "$n.mp4: $INFO"
  [ "$(python3 -I $HERE/tools/pngpx.py "$WORK/$n.png" 0 0 | wc -w)" -ge 3 ] || fail "$n.png unreadable"
done
ok "14 plot examples/ports: compile, run, 960x540 h264 with 240 frames and a PNG still each"

# 14. Explicit container-manager examples draw one still frame per layout.
for n in dashboard-grid layout-workbench; do
  compile "examples/$n.lms" "$WORK/$n.dslb" "$WORK/$n.mp4" "$WORK/$n.png" >/dev/null || fail "compile $n.lms"
  "$HOST" --dsl "$WORK/$n.dslb" >/dev/null 2>"$WORK/$n.err" || { cat "$WORK/$n.err"; fail "run $n.lms"; }
  INFO="$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,width,height,nb_read_frames -of default=nw=1 "$WORK/$n.mp4" | tr '\n' ' ')"
  [ "$INFO" = "codec_name=h264 width=960 height=540 nb_read_frames=1 " ] || fail "$n.mp4: $INFO"
  [ "$(python3 -I "$HERE/tools/pngpx.py" "$WORK/$n.png" 0 0 | wc -w)" -ge 3 ] || fail "$n.png unreadable"
done
ok "2 explicit layout compositions: grid and border/box/flow/place with Skia child widgets"

# Slice 3 chart examples use matrix/vector-field objects and extra Plot scales/kinds.
for n in roc-auc pr-average-precision confusion-matrix poisson-contours cavity-quiver symlog-outlier residual-scatter; do
  compile "examples/$n.lms" "$WORK/$n.dslb" "$WORK/$n.mp4" "$WORK/$n.png" >/dev/null || fail "compile $n.lms"
  "$HOST" --dsl "$WORK/$n.dslb" >/dev/null 2>"$WORK/$n.err" || { cat "$WORK/$n.err"; fail "run $n.lms"; }
  INFO="$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,width,height,nb_read_frames -of default=nw=1 "$WORK/$n.mp4" | tr '\n' ' ')"
  [ "$INFO" = "codec_name=h264 width=960 height=540 nb_read_frames=1 " ] || fail "$n.mp4: $INFO"
  [ "$(python3 -I "$HERE/tools/pngpx.py" "$WORK/$n.png" 0 0 | wc -w)" -ge 3 ] || fail "$n.png unreadable"
done
ok "7 chart examples: ROC, PR, matrix, contours, quiver, symlog, residuals"
compile examples/chart-kinds-workbench.lms "$WORK/chart-kinds-workbench.dslb" "$WORK/chart-kinds-workbench.mp4" "$WORK/chart-kinds-workbench.png" >/dev/null || fail "compile chart-kinds-workbench.lms"
"$HOST" --dsl "$WORK/chart-kinds-workbench.dslb" >/dev/null 2>"$WORK/chart-kinds-workbench.err" || { cat "$WORK/chart-kinds-workbench.err"; fail "run chart-kinds-workbench.lms"; }
INFO="$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,width,height,nb_read_frames -of default=nw=1 "$WORK/chart-kinds-workbench.mp4" | tr '\n' ' ')"
[ "$INFO" = "codec_name=h264 width=960 height=720 nb_read_frames=1 " ] || fail "chart-kinds-workbench.mp4: $INFO"
[ "$(python3 -I "$HERE/tools/pngpx.py" "$WORK/chart-kinds-workbench.png")" = "960x720" ] || fail "chart-kinds-workbench.png size"
ok "chart-kinds-workbench: histogram, box, grouped/stacked bars, donut, dates, bands and inset"
compile examples/standing-wave-field.lms "$WORK/standing-wave-field.dslb" "$WORK/standing-wave-field.mp4" "$WORK/standing-wave-field.png" >/dev/null || fail "compile standing-wave-field.lms"
"$HOST" --dsl "$WORK/standing-wave-field.dslb" >/dev/null 2>"$WORK/standing-wave-field.err" || { cat "$WORK/standing-wave-field.err"; fail "run standing-wave-field.lms"; }
INFO="$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,width,height,nb_read_frames -of default=nw=1 "$WORK/standing-wave-field.mp4" | tr '\n' ' ')"
[ "$INFO" = "codec_name=h264 width=960 height=540 nb_read_frames=60 " ] || fail "standing-wave-field.mp4: $INFO"
ok "standing-wave-field: evolving 33x33 heatmap/quiver, 60-frame H.264 output"

# Slice 4: replay one recorded chart to raster, SVG, PDF, SKP, high-DPI PNG, and a multi-page PDF.
compile examples/vector-outputs.lms "$WORK/vector-outputs.dslb" "$WORK/vector-outputs.mp4" "$WORK/vector-outputs.png" \
  "$WORK/vector-outputs.svg" "$WORK/vector-outputs-2x.png" "$WORK/vector-outputs-3x.png" "$WORK/vector-outputs.pdf" \
  "$WORK/vector-outputs.skp" "$WORK/vector-report.pdf" "$WORK/vector-roundtrip.png" "$WORK/surface-svg-export.svg" \
  "$WORK/surface-pdf-export.pdf" >/dev/null || fail "compile vector-outputs.lms"
"$HOST" --dsl "$WORK/vector-outputs.dslb" >/dev/null 2>"$WORK/vector-outputs.err" || { cat "$WORK/vector-outputs.err"; fail "run vector-outputs.lms"; }
[ "$(python3 -I "$HERE/tools/pngpx.py" "$WORK/vector-outputs.png")" = "960x540" ] || fail "vector-outputs.png dimensions"
[ "$(python3 -I "$HERE/tools/pngpx.py" "$WORK/vector-outputs-2x.png")" = "1920x1080" ] || fail "vector-outputs-2x.png dimensions"
[ "$(python3 -I "$HERE/tools/pngpx.py" "$WORK/vector-outputs-3x.png")" = "2880x1620" ] || fail "vector-outputs-3x.png dimensions"
[ "$(python3 -I "$HERE/tools/pngpx.py" "$WORK/vector-roundtrip.png")" = "960x540" ] || fail "SVG round-trip PNG dimensions"
cmp -s "$WORK/vector-outputs.png" "$WORK/vector-roundtrip.png" || fail "SVG round-trip pixels differ from direct recorded PNG"
python3 - "$WORK" <<'PY' || fail "vector output signatures / multipage PDF"
import pathlib, re, sys
d=pathlib.Path(sys.argv[1])
svg=(d/'vector-outputs.svg').read_text()
assert '<svg' in svg and '<text' not in svg
assert '<svg' in (d/'surface-svg-export.svg').read_text()
assert (d/'vector-outputs.pdf').read_bytes().startswith(b'%PDF-')
pdf=(d/'vector-report.pdf').read_bytes()
assert pdf.startswith(b'%PDF-') and len(re.findall(rb'/Type\s*/Page\b',pdf)) == 2
assert (d/'surface-pdf-export.pdf').read_bytes().startswith(b'%PDF-')
assert (d/'vector-outputs.skp').stat().st_size > 0
PY
ok "vector-outputs: PNG 1x/2x/3x, outlined-text SVG, PDF, SKP, exact SVG PNG round-trip, and two-page PDF"

echo "PASS $pass checks"
