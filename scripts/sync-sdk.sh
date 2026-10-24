#!/usr/bin/env bash
# Vendors cpp/dslvm.hpp from the sibling checkout ../lmcode-cpp (the single source of truth) and pins it in sdk.lock
# (SDK commit + sha256 of the header). `scripts/sync-sdk.sh --verify` only checks the vendored copy against the lock;
# test.sh runs that check, so a header edited in place or drifting from the pin fails the test.
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${LMCODE_CPP:-$HERE/../lmcode-cpp}"
LOCK="$HERE/sdk.lock"; HEADER="$HERE/cpp/dslvm.hpp"
hash() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -c1-64; }
if [ "${1:-}" = "--verify" ]; then
  [ -f "$LOCK" ] || { echo "sdk.lock missing" >&2; exit 1; }
  want="$(sed -n 's/^sha256=//p' "$LOCK")"; have="$(hash "$HEADER")"
  [ "$want" = "$have" ] || { echo "cpp/dslvm.hpp sha256 $have differs from sdk.lock $want: run scripts/sync-sdk.sh (or restore the pinned header)" >&2; exit 1; }
  echo "cpp/dslvm.hpp matches sdk.lock ($(sed -n 's/^commit=//p' "$LOCK"))"; exit 0
fi
[ -f "$SDK/include/dslvm.hpp" ] || { echo "lmcode-cpp checkout not found at $SDK (set LMCODE_CPP)" >&2; exit 1; }
cp "$SDK/include/dslvm.hpp" "$HEADER"
commit="$(git -C "$SDK" rev-parse HEAD)"; dirty=""; git -C "$SDK" diff --quiet -- include/dslvm.hpp || dirty=" (uncommitted changes in the SDK checkout)"
printf 'repo=https://github.com/lmctl-ai/lmcode-cpp\ncommit=%s\nsha256=%s\n' "$commit" "$(hash "$HEADER")" >"$LOCK"
echo "synced cpp/dslvm.hpp from lmcode-cpp $commit$dirty"
