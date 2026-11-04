#!/usr/bin/env python3
"""Fail closed on private provenance in a staged public tree; never rewrite it."""
import argparse
import json
from pathlib import Path
import re
import sys
from urllib.parse import unquote

PRIVATE = re.compile(r'/home/|/tmp/|lmctlhq|durable-memory/|(?<![A-Za-z0-9_-])cpp/|(?<![A-Za-z0-9_-])tests/|schema/lmvideo\.dsl\.json|scripts/cycles/|scripts/(?:skia\.lock|sync-sdk\.sh|fetch-skia\.sh)|\bsdk\.lock\b', re.I)
MAX_BYTES = 16 * 1024 * 1024


def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, item in value.items():
            yield key
            yield from strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from strings(item)


def markers(text):
    found = set()
    # Public upstream attribution in the byte-exact OIIO notice, not a private path.
    text = text.replace("http://www.adp-gmbh.ch/cpp/common/base64.html", "upstream-base64-attribution")
    # JSON decoding below handles escapes; percent-decoding also catches URL paths.
    for _ in range(3):
        found.update(m.group(0).lower() for m in PRIVATE.finditer(text))
        decoded = unquote(text)
        if decoded == text:
            break
        text = decoded
    return found


def inspect(root):
    if not root.is_dir() or root.is_symlink():
        raise ValueError('stage must be a real directory')
    failures = []
    count = 0
    for path in sorted(root.rglob('*')):
        name = path.relative_to(root).as_posix()
        if path.is_symlink():
            failures.append({'file': name, 'reason': 'symlink is not a staged file'})
            continue
        if '.git' in path.relative_to(root).parts:
            failures.append({'file': name, 'reason': 'git metadata is not public content'})
            continue
        if path.is_dir():
            continue
        if not path.is_file() or path.stat().st_size > MAX_BYTES:
            failures.append({'file': name, 'reason': 'nonregular or oversized staged file'})
            continue
        count += 1
        data = path.read_bytes()
        found = markers(name) | markers(data.decode('utf-8', errors='replace'))
        if path.suffix.lower() == '.json':
            try:
                value = json.loads(data)
            except (ValueError, UnicodeError):
                failures.append({'file': name, 'reason': 'invalid JSON'})
                continue
            for text in strings(value):
                found.update(markers(text))
        if found:
            failures.append({'file': name, 'reason': 'private provenance', 'markers': sorted(found)})
    if not count:
        failures.append({'file': '.', 'reason': 'empty stage'})
    return {'status': 'FAIL' if failures else 'PASS', 'files_checked': count, 'failures': failures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = inspect(args.stage)
    output = json.dumps(report, indent=2) + '\n'
    if args.report:
        args.report.write_text(output)
    else:
        print(output, end='')
    print(f"{report['status']}: {report['files_checked']} files, {len(report['failures'])} failures", file=sys.stderr)
    return 1 if report['failures'] else 0


if __name__ == '__main__':
    sys.exit(main())
