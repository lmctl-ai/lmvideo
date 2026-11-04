#!/usr/bin/env python3
"""Check every staged example with explicit versioned hosts; no downloads or skips."""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import tempfile


def command(binary, *args):
    """Return a native process command, including Windows .cmd shims."""
    binary = Path(binary)
    if os.name == 'nt' and binary.suffix.lower() in {'.cmd', '.bat'}:
        return ['cmd.exe', '/d', '/c', str(binary), *map(str, args)]
    return [str(binary), *map(str, args)]


def install_alias(target, binary):
    """Create a PATH alias, falling back to a copy where symlinks are denied."""
    try:
        target.symlink_to(binary)
    except (OSError, NotImplementedError):
        shutil.copy2(binary, target)
        if os.name != 'nt':
            target.chmod(0o755)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage', type=Path, required=True)
    p.add_argument('--host', type=Path, required=True)
    p.add_argument('--cad', type=Path, required=True)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--version', required=True, help='exact lmvideo version without v')
    p.add_argument('--cad-version', default=json.loads((Path(__file__).resolve().parents[1]/'toolchain.lock.json').read_text(encoding='utf-8'))['cad']['version'])
    p.add_argument('--compiler-version', default='0.2.460')
    p.add_argument('--expected-count', type=int, default=len(json.loads((Path(__file__).resolve().parents[1]/'packaging/release-examples.lock.json').read_text(encoding='utf-8'))['examples']))
    p.add_argument('--report', type=Path, required=True)
    args = p.parse_args()
    report = {'status': 'FAIL', 'hosts': {}, 'examples': []}
    try:
        stage = args.stage.resolve(strict=True)
        binaries = {'lmvideo': args.host.resolve(strict=True), 'lmcad': args.cad.resolve(strict=True),
                    'lmscript': args.compiler.resolve(strict=True)}
        versions = {'lmvideo': args.version, 'lmcad': args.cad_version, 'lmscript': args.compiler_version}
        for name, binary in binaries.items():
            result = subprocess.run(command(binary, '--version'), text=True, capture_output=True, timeout=30, check=True)
            actual = result.stdout.strip()
            if actual != name + ' ' + versions[name]:
                raise ValueError(f'{name}: expected {versions[name]}, got {actual}')
            report['hosts'][name] = {'version': actual, 'sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
        examples = sorted((stage/'examples').rglob('*.lms'))
        if len(examples) != args.expected_count:
            raise ValueError(f'expected {args.expected_count} examples, found {len(examples)}')
        with tempfile.TemporaryDirectory(prefix='lmvideo-release-check-') as temp:
            bin_dir = Path(temp)
            for name, binary in binaries.items():
                if name == 'lmscript':
                    continue  # Invoke its original shim; copied Windows shims lose their package-relative entrypoint.
                suffix = binary.suffix if os.name == 'nt' and binary.suffix.lower() in {'.exe', '.cmd', '.bat'} else ''
                install_alias(bin_dir/(name+suffix), binary)
            env = dict(os.environ, PATH=str(bin_dir)+os.pathsep+os.environ.get('PATH', ''))
            # --check deliberately refuses implicit discovery. Populate this run's
            # cache from the exact version-checked binaries, without installing tools.
            discovery = bin_dir/'discover-hosts.lms'
            discovery.write_text('load_schema(cli="lmvideo");\nload_schema(cli="lmcad");\n')
            subprocess.run(command(binaries['lmscript'], discovery), cwd=stage, env=env,
                           capture_output=True, text=True, timeout=120, check=True)
            for example in examples:
                try:
                    checked = subprocess.run(command(binaries['lmscript'], '--check', str(example)), cwd=stage,
                                             env=env, capture_output=True, text=True, timeout=120)
                    record = {'example': example.relative_to(stage).as_posix(), 'exit_code': checked.returncode,
                              'stdout': checked.stdout, 'stderr': checked.stderr}
                except subprocess.TimeoutExpired:
                    record = {'example': example.relative_to(stage).as_posix(), 'exit_code': 124, 'stderr': 'check timeout'}
                report['examples'].append(record)
        report['passed'] = sum(r['exit_code'] == 0 for r in report['examples'])
        report['failed'] = len(examples) - report['passed']
        report['skipped'] = 0
        report['status'] = 'PASS' if report['failed'] == 0 else 'FAIL'
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(report['status'], report.get('error', f"{report.get('passed',0)} passed, {report.get('failed',0)} failed, 0 skipped"))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
