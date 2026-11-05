#!/usr/bin/env python3
"""Credential-free native job integrity check against trusted job outputs."""
import os
import sys
from pathlib import Path
from release_policy import PAYLOADS, digest, validate_files

root = Path(sys.argv[1])
expected = sys.argv[2]
if digest((root / "SHA256SUMS").read_bytes()) != expected:
    raise SystemExit("candidate checksum identity changed in the artifact handoff")
validate_files(root)
if os.name != "nt":
    # GitHub artifact ZIPs do not preserve executable mode. Restore it only
    # after the frozen hashes and provenance have been checked.
    for name in PAYLOADS:
        if name.startswith("lmvideo-") and not name.endswith(".tar.gz"):
            binary = root / name
            binary.chmod(binary.stat().st_mode | 0o111)
print("Native candidate bytes match the verified handoff")
