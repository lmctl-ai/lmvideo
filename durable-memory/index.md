# lmvideo durable facts

## DSL v4 base — S1′ (2026-10-18)

Pinned cpp-dsl-sdk `cd7396b11cf175cffa8028fec1de29635051355a`: canonical imports,
context/locals/max_stack/line sections, wrapping i64 integers and remainder, no VM resource caps.
Collections and lmvideo's domain checks remain host-owned; they were not removed.
`LMSCRIPT_BIN=../lmctl-mono/lmscript/bin/lmscript nice bash test.sh` passed 20 checks
against the built v4 compiler, including rendering, 60-frame video and the host's Path/Paint check.
Release 0.2.328 emits v3 and is not compatible; no release promotion occurred.

## S3 SDK adoption (2026-10-18)

Pinned cpp-dsl-sdk `126b5927dd684044fac0fa17825d6cbeba02b84c`: structured range loops with
snapshot bounds and compiler-owned counters, structural termination and no runtime budget/cap.
Use the matching built mono compiler; released 0.2.328 is still v3.

## S4 common math and DSL-owned animation

The SDK's common math replaces native math registrations; mix remains a host color operation.
One chunk configures then calls video.begin(), frame() from a DSL loop, and end(). The host owns
surface/encoder objects, validates their lifecycle and retains its domain limits.
Keyframes is a deque-backed host collection: add()/len()/at(); handles stay stable after append.
TemplateSvg.render(x,y,color) instantiates an SVG string. Examples/template.lms uses nested frame,
attribute and collection loops, interpolates with lerp/ease, and writes both MP4 and final PNG.
The result includes bytecode_bytes, instructions (VM dispatches only), and wall_ms (chunk execution,
including encoding/finalization, excluding compilation/loading). No execution budget is added.

S4 test.sh: PASS 22 checks (77 Skia, 32 encoder checks retained). Template: 12 frames, 2220 bytes,
1758 dispatches, 47 ms in one shared-box run; exact paths influence bytes. First/mid/last pixels,
collection markers, repeat PNG and decoded MP4 equality are gated. SDK pin is 90e1000 (see sdk.lock).

## S2 schema and SDK adoption

Schema 2 requires signature arrays. The SDK binds complete canonical imports, allowing multiple
signatures per name without dynamic overload dispatch. Self is expanded to the receiver type at
compile/host-schema-read time; the VM adds no identity/type checks or limits. See sdk.lock for pin.
Paint/Path use self; this host intentionally implements singleton signature arrays and refuses multiple entries.

## Current integration target — DSL v5

The host now targets the v5 compiler/runtime. The S1–S4 records and measurements above remain historical v4 evidence; the
vendored SDK pin is refreshed by the integration owner after the v5 runtime is verified.

S5 host coverage is verified: VM division errors and `out.fail` application errors assert source line/column locations, with
the existing video output protocol preserved. The matching SDK and compiler are verified below.

V5 integration verified with mono compiler and SDK f140fa3: test.sh PASS 22 checks; both runtimes pass the same 42 regenerated vectors. No release promotion.

Final v5 verifier refresh: SDK b8fd285, PASS 22 checks; both runtimes pass all 43 regenerated vectors. This supersedes the earlier f140fa3 integration pin.

S5 verified with SDK a0bebcd: PASS 23 checks. Exact physical line/column assertions cover existing host failures and computed division by zero; both SDKs pass all 50 shared vectors. Existing result codes and host checks are retained. No release promotion.
