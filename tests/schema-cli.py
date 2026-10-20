"""DSL host flag standard (lmscript R42): exact shared --dsl-schema summary format, byte-identical
--dsl-schema --json, and strict refusal (exit 2, empty stdout) of every other combination, including --schema FILE.
usage: python3 tests/schema-cli.py HOST SCHEMA.dsl.json"""
from pathlib import Path
import subprocess,sys
host,schema=sys.argv[1],sys.argv[2]
def call(*args): return subprocess.run([host,*args],capture_output=True,check=True).stdout
sigs=call('--dsl-signatures').decode().splitlines()
expected=[s for s in sigs if s.startswith(('context ','fn '))]
groups={}
for s in sigs:
 if s.startswith('method '):
  body=s[7:];groups.setdefault(body.split('.')[0],[]).append(body)
for owner,methods in groups.items():
 expected.append(owner+':');expected.extend('  '+method for method in methods)
summary=call('--dsl-schema')
assert summary.decode()=='\n'.join(expected)+'\n','summary differs from shared canonical format'
assert summary==call('--dsl-schema'),'unstable summary ordering'
assert call('--dsl-schema','--json')==Path(schema).read_bytes(),'--dsl-schema --json differs from the schema file bytes'
for args in [('--schema',schema),('--dsl','missing.dslb','--schema',schema),('--schema',),('--schema','--json'),('--json',),('--json','--dsl-schema'),
             ('--dsl-schema','--dsl','missing'),('--dsl-schema','--json','extra'),('--dsl-schema',schema),('--dsl-signatures','--json'),
             ('--dsl-signatures','--dsl-schema'),('--dsl','missing','--dsl-schema'),('--dsl','missing','--json'),('--dsl','missing','--dsl-signatures')]:
 r=subprocess.run([host,*args],capture_output=True)
 assert r.returncode==2 and not r.stdout,(args,r)
print(f'{len(sigs)} canonical members, exact schema bytes, stable shared summary format and invalid flags verified')
