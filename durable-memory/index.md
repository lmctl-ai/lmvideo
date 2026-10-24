# lmvideo durable memory

lmvideo is an experimental C++ library and lmscript host for raster drawing, SVG, and short video output. The host owns the process and native resources; a single compiled DSL chunk configures the run and performs its loops.

Public entry points:

| File | Purpose |
| --- | --- |
| `README.md` | installation, examples, dependency relationship, and public usage |
| `docs/design.md` | design rationale and dependency boundaries |
| `schema/lmvideo.dsl.json` | host object and method contract |
| `scripts/fetch-skia.sh`, `scripts/skia.lock` | pinned external Skia download and checksum |
| `durable-memory/03-build-run-test.md` | repeatable validation procedure |
| `durable-memory/04-gotchas.md` | build and runtime cautions |
| `durable-memory/05-open-items.md` | deliberately deferred work |
| `durable-memory/06-ffmpeg-in-process-feasibility.md` | evaluation (with a reproducible spike in `tools/ffmpeg-spike/`) of linking libav instead of running `ffmpeg` |
| `durable-memory/07-plot-proposal.md` | typed Plot/Series API and implementation facts for high-level Skia charts |

The current source tree is prepared for lmscript 0.2.347 or newer from `PATH`. The C++ runtime header remains pinned by `sdk.lock`; the pin is unchanged during publication preparation.

The bundled font license, Skia provenance, actual archive notices, and binary redistribution follow-up are recorded in `THIRD_PARTY_NOTICES.md`.
