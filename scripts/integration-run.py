#!/usr/bin/env python3
"""Run every public example with the installed hosts and write a JSON/markdown report.

Cross-platform (Linux, macOS, Windows). Runs from the repository root so the
examples' relative output paths (build/, images/) resolve as documented.
Exit status is non-zero if any example fails or is skipped, unless --allow-skips.
"""
import argparse, json, os, platform, shutil, subprocess, sys, time
from pathlib import Path


def skip_table():
    path = os.environ.get('LMCTL_UNAVAILABLE_EXAMPLES_FILE')
    table = {}
    if path and Path(path).is_file():
        for line in Path(path).read_text(encoding='utf-8').splitlines():
            if '\t' in line:
                name, reason = line.split('\t', 1)
                table[Path(name).as_posix()] = reason
    return table


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--report', type=Path, default=Path('integration-report.json'))
    p.add_argument('--summary', type=Path, help='markdown summary file (e.g. $GITHUB_STEP_SUMMARY)')
    p.add_argument('--timeout', type=int, default=600, help='seconds per example')
    p.add_argument('--allow-skips', action='store_true')
    p.add_argument('--only', help='substring filter for example paths')
    a = p.parse_args()
    root = Path.cwd()
    lmscript = shutil.which('lmscript')  # resolves lmscript.cmd on Windows
    if not lmscript:
        print('lmscript not found on PATH', file=sys.stderr)
        return 2
    skips = skip_table()
    examples = sorted(x for x in (root / 'examples').rglob('*.lms') if not a.only or a.only in x.as_posix())
    results = []
    for ex in examples:
        rel = ex.relative_to(root).as_posix()
        if rel in skips:
            results.append({'example': rel, 'status': 'SKIP', 'reason': skips[rel]})
            continue
        shutil.rmtree(root / 'build', ignore_errors=True)  # examples must not depend on, or collide with, another example's outputs
        start = time.time()
        try:
            r = subprocess.run([lmscript, rel], cwd=root, capture_output=True, text=True,
                               timeout=a.timeout, encoding='utf-8', errors='replace')
            status, code, tail = ('PASS' if r.returncode == 0 else 'FAIL'), r.returncode, (r.stdout + r.stderr)[-1200:]
        except subprocess.TimeoutExpired:
            status, code, tail = 'FAIL', 124, f'timeout after {a.timeout}s'
        rec = {'example': rel, 'status': status, 'exit_code': code, 'seconds': round(time.time() - start, 1)}
        if status != 'PASS':
            rec['detail'] = tail
        results.append(rec)
        print(f'{status} {rel} ({rec["seconds"]}s)', flush=True)
    counts = {s: sum(r['status'] == s for r in results) for s in ('PASS', 'FAIL', 'SKIP')}
    report = {'platform': f'{platform.system()} {platform.machine()}', 'counts': counts, 'examples': results}
    a.report.parent.mkdir(parents=True, exist_ok=True)
    a.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    lines = [f'### {report["platform"]}: {counts["PASS"]} pass, {counts["FAIL"]} fail, {counts["SKIP"]} skip', '']
    lines += [f'- FAIL `{r["example"]}`: {r.get("detail", "")[-700:]!r}' for r in results if r['status'] == 'FAIL']
    lines += [f'- SKIP `{r["example"]}`: {r["reason"]}' for r in results if r['status'] == 'SKIP']
    text = '\n'.join(lines) + '\n'
    print(text)
    if a.summary:
        with a.summary.open('a', encoding='utf-8') as fh:
            fh.write(text)
    bad = counts['FAIL'] or (counts['SKIP'] and not a.allow_skips)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
