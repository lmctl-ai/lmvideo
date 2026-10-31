# Migration fixtures

Before/after pairs for the legacy-to-lmvideo source migration described in
[Migrating from lmdraw, lmmagnum, lmfilament and lmimg](../migrating-from-legacy-hosts.md).

Each `*.before.lms` runs on its pinned legacy host; each `*.after.lms` is the
migration tool's output and runs on lmvideo. The `negative-*.before.lms`
files exercise constructs that intentionally have no mechanical mapping: the
tool reports located LMV7xxx diagnostics and leaves those blocks
byte-identical. `manifest.json` records the output comparators used to review
visual equivalence (SSIM/geometry probes; 3D renders are declared-equivalent,
not pixel-equal, because the lighting model changed from Blinn-Phong/Lambert
previews to GGX PBR).
