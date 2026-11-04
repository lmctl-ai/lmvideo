#!/usr/bin/env python3
"""Inspect the downloaded SFX payload, including every ELF, without executing it."""
import argparse,hashlib,io,json,re,struct,subprocess,tarfile,tempfile
from pathlib import Path,PurePosixPath
SYSTEM={'libc.so.6','libm.so.6','libdl.so.2','libpthread.so.0','librt.so.1','libresolv.so.2','ld-linux-x86-64.so.2','ld-linux-aarch64.so.1'}
def verify(asset,arch,version):
    with asset.open('rb')as f:
        f.seek(-96,2);magic,offset,size,digest=struct.unpack('<16sQQ64s',f.read(96))
        if magic!=b'LMVRT-PAYLOAD1\0\0' or offset+size+96!=asset.stat().st_size:raise ValueError('invalid runtime footer')
        f.seek(offset);payload=f.read(size)
    if hashlib.sha256(payload).hexdigest().encode()!=digest:raise ValueError('payload SHA mismatch')
    files={}
    with tarfile.open(fileobj=io.BytesIO(payload),mode='r:gz')as tar:
        for m in tar:
            if m.isdir():continue
            path=PurePosixPath(m.name)
            if not m.isfile()or path.is_absolute()or'..'in path.parts or m.name in files:raise ValueError('unsafe runtime member')
            if m.size>512*1024*1024:raise ValueError('oversized runtime member')
            files[m.name]=tar.extractfile(m).read()
    manifest=json.loads(files['MANIFEST.json'])
    if manifest['version']!=version or manifest['arch']!=arch:raise ValueError('runtime version/architecture mismatch')
    locked={}
    for line in files['SHA256SUMS'].decode().splitlines():
        digest,name=line.split('  ',1)
        if name in locked or hashlib.sha256(files[name]).hexdigest()!=digest:raise ValueError('inner checksum mismatch')
        locked[name]=digest
    if set(locked)!=set(files)-{'SHA256SUMS'}:raise ValueError('unlocked runtime member')
    for name,digest in manifest['files'].items():
        if hashlib.sha256(files[name]).hexdigest()!=digest:raise ValueError('manifest mismatch '+name)
    machine=62 if arch=='x86_64'else 183;count=0
    with tempfile.TemporaryDirectory(prefix='lmvideo-elf-verify-')as t:
        path=Path(t)/'elf'
        for name,data in files.items():
            if not data.startswith(b'\x7fELF'):continue
            if data[4:6]!=b'\x02\x01' or struct.unpack_from('<H',data,18)[0]!=machine:raise ValueError('mixed ELF architecture')
            path.write_bytes(data);dynamic=subprocess.check_output(['readelf','-d',str(path)],text=True);versions=subprocess.check_output(['readelf','--version-info',str(path)],text=True)
            for v in re.findall(r'GLIBC_(\d+\.\d+(?:\.\d+)?)',versions):
                x=tuple(map(int,v.split('.')));x=x+(0,)*(3-len(x))
                if x>(2,35,0):raise ValueError('glibc floor exceeded by '+name)
            if 'GLIBC_ABI_DT_RELR'in versions:raise ValueError('newer glibc loader ABI in '+name)
            for rpath in re.findall(r'\((?:RUNPATH|RPATH)\).*?\[(.*?)\]',dynamic):
                if any(not p.startswith('$ORIGIN')for p in rpath.split(':')):raise ValueError('nonrelocated search path')
            for dependency in re.findall(r'\(NEEDED\).*?\[(.*?)\]',dynamic):
                if dependency not in SYSTEM and 'lib/'+dependency not in files:raise ValueError('incomplete closure: '+dependency)
            count+=1
    if not any('OpenImageDenoise_device_cpu.so'in n for n in files)or not any(n.startswith('lib/oiio/')for n in files)or not any(n.endswith('.tza')for n in files):raise ValueError('missing runtime plugin/weights')
    return {'status':'PASS','version':version,'arch':arch,'elfs':count,'files':len(files),'payload_sha256':hashlib.sha256(payload).hexdigest(),'asset_sha256':hashlib.sha256(asset.read_bytes()).hexdigest()}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--asset',type=Path,required=True);p.add_argument('--arch',choices=['x86_64','arm64'],required=True);p.add_argument('--version',required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args();result=verify(a.asset,a.arch,a.version);a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=2)+'\n');print('PASS verified complete relocated',a.arch,'runtime')
if __name__=='__main__':main()
