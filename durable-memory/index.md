# lmvideo durable facts

## DSL v4 base — S1′ (2026-10-18)

Pinned cpp-dsl-sdk `cd7396b11cf175cffa8028fec1de29635051355a`: canonical imports,
context/locals/max_stack/line sections, wrapping i64 integers and remainder, no VM resource caps.
Collections and lmvideo's domain checks remain host-owned; they were not removed.
The existing host-owned frame loop remains until S3/S4. No later-slice features are claimed.
`LMSCRIPT_BIN=../lmctl-mono/lmscript/bin/lmscript nice bash test.sh` passed 20 checks
against the built v4 compiler, including rendering, 60-frame video and the host's Path/Paint check.
Release 0.2.328 emits v3 and is not compatible; no release promotion occurred.

## S3 SDK adoption (2026-10-18)

Pinned cpp-dsl-sdk `126b5927dd684044fac0fa17825d6cbeba02b84c`: structured range loops with
snapshot bounds and compiler-owned counters, structural termination and no runtime budget/cap.
Use the matching built mono compiler; released 0.2.328 is still v3.
