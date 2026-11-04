#!/usr/bin/env python3
"""Require raw host stdout to match the LF schema and its canonical hash lock."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def verify_bytes(actual, expected, digest):
    if not re.fullmatch(r'[0-9a-f]{64}', digest):
        raise ValueError('invalid schema SHA256 lock')
    if b'\r' in expected or hashlib.sha256(expected).hexdigest() != digest:
        raise ValueError('source schema differs from its locked LF bytes')
    if b'\r' in actual:
        raise ValueError('host schema stdout contains CR bytes (Windows CRT text mode)')
    if actual != expected or hashlib.sha256(actual).hexdigest() != digest:
        raise ValueError('host schema stdout differs from locked schema sha256 '+digest)


def verify(host, root=ROOT):
    expected = (root/'schema/lmvideo.dsl.json').read_bytes()
    digest = (root/'schema/lmvideo.dsl.json.sha256').read_text(encoding='ascii').strip()
    verify_bytes(expected, expected, digest)
    # Never enable text/universal-newline decoding: it would hide CRT CRLF.
    result = subprocess.run([str(host), '--lmcode-schema', '--json'], capture_output=True, timeout=120)
    if result.returncode:
        raise ValueError('host schema command failed: '+result.stderr.decode('utf-8', errors='replace'))
    actual = result.stdout
    verify_bytes(actual, expected, digest)
    return {'status': 'PASS', 'schema_sha256': digest, 'schema_bytes': len(actual)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = verify(args.host.resolve())
    if args.report:
        args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8', newline='\n')
    print('PASS raw schema stdout equals locked LF bytes: '+report['schema_sha256'])


if __name__ == '__main__':
    main()
