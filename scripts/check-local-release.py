#!/usr/bin/env python3
"""Real pinned installer rehearsal, raster/Cycles smoke and exact pinned-example gate."""
import argparse,hashlib,http.server,json,os,platform,shutil,struct,subprocess,sys,tempfile,threading,urllib.parse,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CAD_VERSION=json.loads((ROOT/'toolchain.lock.json').read_text(encoding='utf-8'))['cad']['version']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(cmd,cwd,env=None,timeout=300):
    argv=[str(x)for x in cmd]
    if os.name=='nt' and Path(argv[0]).suffix.lower() in ('.cmd','.bat'):
        argv=['cmd.exe','/d','/c',*argv]
    p=subprocess.run(argv,cwd=cwd,env=env,text=True,capture_output=True,timeout=timeout)
    if p.returncode:raise RuntimeError('command failed: '+str(cmd)+'\n'+p.stdout[-8000:]+p.stderr[-8000:])
    return p.stdout

def ensure_compiler(requested, work):
    compiler=(requested or Path(shutil.which('lmscript')or'/missing')).resolve()
    if not compiler.is_file() or run([compiler,'--version'],work).strip()!='lmscript 0.2.460':
        prefix=work/'compiler';run([shutil.which('npm')or'npm','install','--prefix',prefix,'@lmctl-ai/lmctl@0.2.460'],work,timeout=300);compiler=(prefix/'node_modules/.bin/lmscript.cmd' if os.name=='nt' else prefix/'node_modules/.bin/lmscript').resolve()
    if run([compiler,'--version'],work).strip()!='lmscript 0.2.460':raise ValueError('exact compiler0.2.460 required')
    return compiler

def alias_external_video_tools(tools, source_path):
    """Keep video tools reachable while excluding existing product binaries."""
    selected = {}
    for name in ('ffmpeg', 'ffprobe'):
        found = shutil.which(name, path=source_path)
        if not found:
            raise ValueError('missing external gate prerequisite on original PATH: '+name)
        binary = Path(found).resolve(strict=True)
        if os.name != 'nt':
            # Windows retains the runner PATH; POSIX excludes Homebrew bins.
            # Symlink rather than copy to preserve Homebrew loader paths.
            (tools/name).symlink_to(binary)
        selected[name] = str(binary)
    return selected


def fresh_path_tail(environment, windows):
    if not windows:
        return "/usr/bin:/bin"
    system_root = environment.get("SystemRoot")
    if not system_root:
        raise ValueError("Windows fresh PATH requires SystemRoot")
    return str(Path(system_root) / "System32")


def main():
    p=argparse.ArgumentParser(description=__doc__);g=p.add_mutually_exclusive_group(required=True);g.add_argument('--asset',type=Path);g.add_argument('--assets',type=Path)
    p.add_argument('--github',action='store_true',help='test the genuine published prerelease; implied by --assets');p.add_argument('--version',required=True);p.add_argument('--work',required=True,type=Path);p.add_argument('--stage',type=Path);p.add_argument('--cad',type=Path);p.add_argument('--compiler',type=Path);p.add_argument('--platform',choices=('auto','linux','macos','windows'),default='auto');a=p.parse_args()
    system=platform.system().lower();native_platform={'darwin':'macos','windows':'windows'}.get(system,'linux') if a.platform=='auto' else a.platform
    arch='arm64'if platform.machine()in('aarch64','arm64')else'x64';asset_name='lmvideo-'+native_platform+'-'+arch+('.exe' if native_platform=='windows' else '');asset=(a.asset or a.assets/asset_name).resolve(strict=True)
    work=a.work.resolve();work.mkdir(parents=True,exist_ok=False);report={'status':'FAIL','version':a.version,'platform':native_platform,'arch':arch,'asset_sha256':sha(asset),'transport':'Local loopback release mirror using the real pinned compiler/installer; not a published GitHub prerelease.'};server=None
    real_release=a.github or bool(a.assets)
    if real_release:report['transport']='Genuine pinned public GitHub prerelease and fresh real installer.'
    env=dict(os.environ)
    for key in ('GH_TOKEN','GITHUB_TOKEN','GH_DEBUG','LD_LIBRARY_PATH','LMVIDEO_CYCLES_MODULE','LMSCRIPT_GITHUB_HTTP','LMSCRIPT_GITHUB_API','DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','OIIO_LIBRARY_PATH'):env.pop(key,None)
    try:
        # Verify every supplied public asset against its downloaded/generated lock.
        lines=(asset.parent/'SHA256SUMS').read_text(encoding='utf-8').splitlines();listed={}
        for line in lines:
            digest,name=line.split(None,1);name=name.lstrip('*')
            if Path(name).name!=name or name in listed or sha(asset.parent/name)!=digest:raise ValueError('invalid release checksum '+name)
            listed[name]=digest
        if listed.get(asset.name)!=sha(asset):raise ValueError('candidate absent from SHA256SUMS')
        verification_targets=[('x86_64','x64'),('arm64','arm64')] if native_platform=='linux' and a.assets else [('x86_64' if arch=='x64' else 'arm64',arch)]
        for architecture,suffix in verification_targets:
            candidate=(a.assets/(('lmvideo-linux-' if native_platform=='linux' else 'lmvideo-'+native_platform+'-')+suffix)).resolve() if a.assets else asset
            verifier='verify-runtime-asset.py' if native_platform=='linux' else 'verify-native-asset.py'
            verify_args=[sys.executable,ROOT/'scripts'/verifier,'--asset',candidate,'--version',a.version,'--report',work/('runtime-'+suffix+'.json')]
            verify_args += ['--arch',architecture] if native_platform=='linux' else ['--platform',native_platform]
            run(verify_args,work)
        compiler=ensure_compiler(a.compiler,work)
        lock=json.loads((ROOT/'packaging/release-examples.lock.json').read_text(encoding='utf-8'))
        if a.stage:stage=a.stage.resolve(strict=True)
        else:
            clone=work/'public-clone';source=work/'public-source';source.mkdir()
            run(['git','init','-q',clone],work);run(['git','-C',clone,'remote','add','origin',lock['repository']],work);run(['git','-C',clone,'fetch','--depth','1','origin',lock['commit']],work)
            archive=work/'public.tar';archive.write_bytes(subprocess.check_output(['git','-C',str(clone),'archive','FETCH_HEAD']))
            run(['tar','-xf',archive,'-C',source],work);stage=work/'public-stage';run([sys.executable,ROOT/'scripts/stage-public-content.py',source,stage],work)
        actual={p.relative_to(stage).as_posix():sha(p)for p in(stage/'examples').rglob('*.lms')}
        expected_count=len(lock['examples'])
        if not expected_count or actual!=lock['examples']:raise ValueError('candidate examples differ from the approved source lock')
        cad=a.cad.resolve(strict=True)if a.cad else work/('lmcad.exe' if native_platform=='windows' else 'lmcad')
        if not a.cad:
            base='https://github.com/lmctl-ai/lmcad/releases/download/v'+CAD_VERSION+'/'
            cad_suffix={'linux':'linux-','macos':'macos-','windows':'windows-'}[native_platform]
            name='lmcad-'+cad_suffix+arch+('.exe' if native_platform=='windows' else '')
            sums=urllib.request.urlopen(base+'SHA256SUMS',timeout=60).read().decode()
            digest=next(line.split()[0]for line in sums.splitlines()if line.split()[-1].lstrip('./*')==name)
            urllib.request.urlretrieve(base+name,cad);cad.chmod(0o755)
            if sha(cad)!=digest:raise ValueError('pinned CAD checksum mismatch')
        if run([cad,'--version'],work).strip()!='lmcad '+CAD_VERSION:raise ValueError('exact CAD'+CAD_VERSION+' required')
        # A private PATH with node only prevents an older installed lmvideo from
        # bypassing install= (PATH deliberately wins in the language contract).
        tools=work/'tools';tools.mkdir();node_path=Path(shutil.which('node') or shutil.which('node.exe') or '/missing-node')
        node_alias=tools/('node.exe' if os.name=='nt' else 'node')
        try: node_alias.symlink_to(node_path)
        except (OSError,NotImplementedError): shutil.copy2(node_path,node_alias)
        report['external_tools']=alias_external_video_tools(tools, os.environ.get('PATH',''))
        home=work/"home with ' quote";home.mkdir();cache=work/'tool-cache'
        path_tail=fresh_path_tail(os.environ,os.name=="nt")
        node_path_entry="" if os.name=="nt" else str(node_path.parent)
        env.update(HOME=str(home),XDG_CACHE_HOME=str(home/'cache'),LMSCRIPT_TOOL_CACHE=str(cache),PATH=str(tools)+(os.pathsep+node_path_entry if node_path_entry else '')+os.pathsep+path_tail,XDG_CONFIG_HOME=str(home/'config'),GH_CONFIG_DIR=str(home/'gh'),LMSCRIPT_GH=str(home/'no-gh'))
        if os.name=='nt': env.update(USERPROFILE=str(home),LOCALAPPDATA=str(home/'localappdata'),APPDATA=str(home/'appdata'))
        if shutil.which('lmvideo',path=env['PATH']):raise ValueError('fresh PATH unexpectedly contains lmvideo')
        requests=[]
        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self,*args):pass
            def do_GET(self):
                path=urllib.parse.urlparse(self.path).path;requests.append(path)
                if path.endswith('/'+asset.name):
                    self.send_response(200);self.send_header('Content-Length',str(asset.stat().st_size));self.end_headers()
                    with asset.open('rb')as f:shutil.copyfileobj(f,self.wfile)
                    return
                if path.endswith('/SHA256SUMS'):data=(asset.parent/'SHA256SUMS').read_bytes()
                elif path.endswith('/v'+a.version):
                    base='http://127.0.0.1:'+str(self.server.server_port)
                    data=json.dumps({'tag_name':'v'+a.version,'prerelease':True,'draft':False,'assets':[{'name':asset.name,'digest':'sha256:'+sha(asset),'browser_download_url':base+'/objects/'+asset.name},{'name':'SHA256SUMS','browser_download_url':base+'/SHA256SUMS'}]}).encode()
                else:self.send_error(404);return
                self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        if not real_release:
            server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start();env['LMSCRIPT_GITHUB_API']='http://127.0.0.1:'+str(server.server_port);env['LMSCRIPT_GITHUB_HTTP']=env['LMSCRIPT_GITHUB_API']
        else:
            release=json.load(urllib.request.urlopen('https://api.github.com/repos/lmctl-ai/lmvideo/releases/tags/v'+a.version,timeout=60))
            if release.get('tag_name')!='v'+a.version or release.get('draft')or not release.get('prerelease'):raise ValueError('candidate is not the requested published prerelease')
            report['release']={'tag':release['tag_name'],'prerelease':True}

        schema_report=work/'schema-bytes.json'
        run([sys.executable,ROOT/'scripts/check-schema-bytes.py',asset,'--report',schema_report],work,env)
        report['schema']=json.loads(schema_report.read_text(encoding='utf-8'))
        images=[]
        try:
            gate_threads=max(1,min(32,int(os.environ.get('LMVIDEO_GATE_THREADS','2'))))
        except ValueError as error:
            raise ValueError('LMVIDEO_GATE_THREADS must be an integer') from error
        for renderer in ('raster','cycles'):
            output=work/(renderer+'.png');bytecode=work/(renderer+'.dslb');source=work/(renderer+'.lms')
            call='s.render_image(96,64,"cycles",16,4,42,true)'if renderer=='cycles'else's.render_image(96,64,2)'
            source.write_text('program = _lmcode_ cli="lmvideo" install="github:lmctl-ai/lmvideo@v'+a.version+'";\nlet s=scene3d().threads('+str(gate_threads)+').ambient(0.0).background("#101820");\ns.camera(camera3d().look_at(0.0,0.0,4.0,0.0,0.0,0.0).perspective(45.0,0.1,20.0));\ns.add(mesh_sphere(1.0,16,24),material("#c98a36",0.1,0.4));s.directional_light(-2.0,3.0,4.0,"#ffffff",2.0);\nlet i='+call+';image_assert_true(i.stddev(0)>0.01);i.text(3.0,12.0,\"CPU\",10.0,1.0,1.0,1.0,1.0);i.save('+json.dumps(str(output))+');\nend _lmcode_\nr=run_lmcode(program);assert(r.ok);println(json_stringify(r.result));\n')
            stdout=run([compiler,source],work,env);(work/(renderer+'.log')).write_text(stdout)
            if struct.unpack('>II',output.read_bytes()[16:24])!=(96,64):raise ValueError('wrong smoke output dimensions')
            images.append({'renderer':renderer,'sha256':sha(output),'dimensions':[96,64]})
        # Exercise the actual pinned CAD native executable, then hand its STL
        # to the freshly installed Cycles runtime. Compilation alone cannot
        # prove the CAD/runtime boundary works on a new native architecture.
        cad_alias=tools/('lmcad.exe' if os.name=='nt' else 'lmcad')
        try:cad_alias.symlink_to(cad)
        except (OSError,NotImplementedError):shutil.copy2(cad,cad_alias)
        mesh_path=work/'cad.stl';cad_code=work/'cad.dslb';render_code=work/'cad-render.dslb';cad_image=work/'cad-cycles.png';cad_source=work/'cad-render.lms'
        cad_source.write_text(
            'cad=_lmcode_ cli="lmcad";\n'
            'let m=material().color(0.72,0.78,0.84);let housing=scene.box(120.0,70.0,80.0,m);let bore=scene.cylinder(15.0,80.0,m);housing.cut(bore);bore.visible(false);scene.export_stl('+json.dumps(str(mesh_path))+');\nend _lmcode_\n'
            'r=run_lmcode(cad);assert(r.ok);\n'
            'p=_lmcode_ cli="lmvideo" install="github:lmctl-ai/lmvideo@v'+a.version+'";\n'
            'let mesh=mesh_load('+json.dumps(str(mesh_path))+');image_assert_true(mesh.triangles()>100);image_assert_close(mesh.bound(3)-mesh.bound(0),120.0,0.01);\n'
            'let s=scene3d().threads('+str(gate_threads)+').background("#edf3f8").ambient(0.15);s.add(mesh,material("#b8c5d1",0.35,0.55)).scale(0.001,0.001,0.001);\n'
            's.camera(camera3d().look_at(0.24,0.14,0.24,0.0,0.0,0.0).perspective(40.0,0.001,2.0));s.directional_light(1.0,3.0,4.0,"#ffffff",1.8);let i=s.render_image(96,64,"cycles",16,4,42,true);image_assert_true(i.stddev(0)>0.01);i.save('+json.dumps(str(cad_image))+');\nend _lmcode_\n'
            'r=run_lmcode(p);assert(r.ok);println(json_stringify(r.result));\n')
        cad_log=run([compiler,cad_source],work,env,timeout=600);(work/'cad-cycles.log').write_text(cad_log)
        if struct.unpack('>II',cad_image.read_bytes()[16:24])!=(96,64):raise ValueError('wrong CAD render dimensions')
        images.append({'renderer':'cad-cycles','sha256':sha(cad_image),'dimensions':[96,64],'stl_sha256':sha(mesh_path)})
        installed_root=cache/'github/lmctl-ai/lmvideo'/('v'+a.version)
        installed=next((candidate for candidate in (installed_root/'lmvideo',installed_root/'lmvideo.exe',installed_root/'bin/lmvideo',installed_root/'bin/lmvideo.exe') if candidate.exists()),installed_root/'lmvideo')
        if sha(installed)!=sha(asset) or (not real_release and not any(path.endswith('/'+asset.name)for path in requests)):raise ValueError('real candidate was not installed from the release fixture')
        if run([installed,'--version'],work,env).strip()!='lmvideo '+a.version:raise ValueError('installed version mismatch')
        output_paths=work/'output-paths'
        run([sys.executable,ROOT/'scripts/check-output-paths.py','--host',installed,'--compiler',compiler,'--work',output_paths],work,env,timeout=600)
        report['output_paths']={'status':'PASS','formats':['png','mp4','glb','stl'],'path_variants':['absolute','relative','spaces','unicode','replacement']}
        compat=work/'compatibility.json';run([sys.executable,ROOT/'scripts/check-release-example-compatibility.py','--stage',stage,'--host',installed,'--cad',cad,'--compiler',compiler,'--version',a.version,'--expected-count',str(expected_count),'--report',compat],work,env,timeout=1800)
        result=json.loads(compat.read_text(encoding='utf-8'))
        if(result['passed'],result['failed'],result['skipped'])!=(expected_count,0,0):raise ValueError('exact compatibility gate failed')
        report.update(status='PASS',gate_threads=gate_threads,images=images,requests=requests,installed_sha256=sha(installed),compiler_sha256=sha(compiler),cad_sha256=sha(cad),compatibility={'passed':expected_count,'failed':0,'skipped':0,'report_sha256':sha(compat)},example_lock_sha256=sha(ROOT/'packaging/release-examples.lock.json'))
    except Exception as error:
        report['error']=str(error)
    finally:
        if server:server.shutdown();server.server_close()
        (work/'local-gate.json').write_text(json.dumps(report,indent=2)+'\n')
    print(report['status'],report.get('error',f"fresh installer + real raster/Cycles +{report.get('compatibility',{}).get('passed',0)} approved examples"))
    return 0 if report['status']=='PASS'else 1
if __name__=='__main__':raise SystemExit(main())
