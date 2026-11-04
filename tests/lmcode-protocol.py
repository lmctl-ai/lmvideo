#!/usr/bin/env python3
"""Focused lmcode CLI protocol checks; full rendering tests remain elsewhere."""
import json
import os
import subprocess
import shutil
import tempfile
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
command = [sys.argv[1] if len(sys.argv) > 1 else str(root / 'build/lmvideo')]
schema_path = Path(os.environ.get('LMVIDEO_QUALIFY_SCHEMA', root / 'schema/lmvideo.dsl.json'))
schema = schema_path.read_bytes()

def call(*args):
    return subprocess.run([*command, *args], capture_output=True)

assert call('--lmcode-schema').stdout == schema
assert call('--lmcode-schema', '--json').stdout == schema
assert call('--lmcode-signatures').returncode == 0
for arguments, code, phase in ((('--lmcode',), 2, 'cli'), (('--lmcode', str(root/'no-such-program.bin')), 3, 'load')):
    result = call(*arguments)
    lines = result.stdout.decode().splitlines()
    assert len(lines) == 3 and lines[0] == 'LMCODE_RETURN_BEGIN' and lines[2] == 'LMCODE_RETURN_END', result
    payload = json.loads(lines[1])
    assert result.returncode == code and payload['phase'] == phase and payload['ok'] is False, payload
for old in ('--dsl', '--dsl-schema', '--dsl-signatures'):
    result = call(old)
    assert result.returncode == 2, (old, result.returncode, result.stdout, result.stderr)
    frame = result.stdout.decode()
    assert 'LMCODE_RETURN_BEGIN\n' in frame and '"code":"CLI_USAGE"' in frame, frame

compiler = Path(os.environ.get('LMSCRIPT_REAL_BIN') or os.environ.get('LMSCRIPT_BIN') or shutil.which('lmscript') or '')

def compiler_command(source):
    arguments = [str(compiler), str(source)]
    if os.name == 'nt' and compiler.suffix.lower() in {'.cmd', '.bat'}:
        return ['cmd.exe', '/d', '/c', *arguments]
    return arguments

if compiler.exists():
    with tempfile.TemporaryDirectory(prefix='lmvideo-lmcode-protocol-') as directory:
        directory = Path(directory)
        schema_path = schema_path.resolve()
        for name, body, expected in (
            ('null', 'lmcode_return_null();', 'null'),
            ('scalar', 'lmcode_return_set("scalar");', '"scalar"'),
            ('integer', 'lmcode_return_set(42);', '42'),
            ('map', 'lmcode_return_put("ok", true);', '{"ok":true}'),
        ):
            source = directory / (name + '.lms')
            source.write_text(f'p = _lmcode_ cli="lmvideo";\nvideo.size(2,2); video.frames(1); video.output({json.dumps(str(directory / (name + ".png")))}); video.begin(); video.frame(); video.end();\n{body}\nend _lmcode_\nr=run_lmcode(p); assert(r.ok); println(json_stringify(r.result));\n')
            run_env = dict(os.environ, PATH=str(Path(command[0]).resolve().parent) + os.pathsep + str(compiler.parent) + os.pathsep + os.environ.get('PATH',''))
            result = subprocess.run(compiler_command(source), env=run_env, capture_output=True, text=True)
            assert result.returncode == 0, (name, result.stdout, result.stderr)
            assert result.stdout.strip() == expected, (name, result.stdout, result.stderr)
print('PASS lmcode schema/signatures and retired flag protocol')

# A last-role part can be submitted before a first-role part. Result slots stay
# in submission order even though rendering runs in role order.
with tempfile.TemporaryDirectory(prefix='lmvideo-builder-') as directory:
    directory = Path(directory)
    source = directory/'builder.lms'
    output = json.dumps(str(directory/'builder.png'))
    source.write_text('setup=_lmcode_ cli="lmvideo";\nvideo.size(2,2);video.frames(1);video.output('+output+');video.begin();video.frame();\nend _lmcode_\n'
        'finish=_lmcode_ cli="lmvideo";\nvideo.end();lmcode_return_set("tail");\nend _lmcode_\n'
        'b=lmcode_build("lmvideo").add(finish,["--last","$DSLFILE"]).add(setup,["--first","$DSLFILE"]);\n'
        'r=run_lmcode(b);assert(r.ok);assert(r.result[0]=="tail");assert(r.result[1]==null);\n')
    result = subprocess.run(compiler_command(source),env=run_env,capture_output=True,text=True)
    assert result.returncode == 0,(result.stdout,result.stderr)
print('PASS explicit null/scalar/map ownership, missing-input phases and submitted builder result order')

# Equal-role chunks execute in submission order, while result slots retain the
# original mixed-role submission indices.
with tempfile.TemporaryDirectory(prefix='lmvideo-builder-ties-') as directory:
    directory = Path(directory)
    output = json.dumps(str(directory/'ties.png'))
    parts = [('last_a', 'video.end();', 'last-a'),
             ('normal_a', '', 'normal-a'),
             ('first_a', 'video.size(2,2);video.frames(1);video.output('+output+');video.begin();video.frame();', 'first-a'),
             ('normal_b', '', 'normal-b'), ('first_b', '', 'first-b'),
             *[('extra_'+str(i), '', 'extra-'+str(i)) for i in range(24)], ('last_b', '', 'last-b')]
    source = directory/'ties.lms'
    blocks = []
    for name, setup, label in parts:
        returned = '' if name == 'last_b' else 'lmcode_return_set('+json.dumps(label)+');'
        blocks.append(name+'=_lmcode_ cli="lmvideo";\n'+setup+'out.print('+json.dumps(label)+');'+returned+'\nend _lmcode_\n')
    roles = {'last_a':'--last', 'first_a':'--first', 'first_b':'--first', 'last_b':'--last'}
    blocks.append('b=lmcode_build("lmvideo")'+''.join('.add('+name+(','+json.dumps([roles[name], '$DSLFILE']) if name in roles else '')+')' for name, _, _ in parts)+';\n')
    blocks.append('r=run_lmcode(b);assert(r.ok);')
    for i, (_, _, label) in enumerate(parts[:-1]):
        blocks.append('assert(r.result['+str(i)+']=='+json.dumps(label)+');')
    blocks.append('println(json_stringify(r.result['+str(len(parts)-1)+'].output));\n')
    source.write_text(''.join(blocks))
    result = subprocess.run([str(compiler),str(source)],env=run_env,capture_output=True,text=True)
    assert result.returncode == 0,(result.stdout,result.stderr)
    assert json.loads(result.stdout) == ['first-a','first-b','normal-a','normal-b']+['extra-'+str(i) for i in range(24)]+['last-a','last-b'], result.stdout
print('PASS equal-role execution ties and mixed-role submitted result indices')
