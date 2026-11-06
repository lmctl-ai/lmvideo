"""Offline release regression controls. No network, actual tokens or release mutation."""
import hashlib,json,os,runpy,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def text(file):
 p=ROOT/file
 return p.read_text() if p.exists() else ''

def job_blocks():
 data={}
 for file in ['.github/workflows/qualify-private-draft.yml','.github/workflows/qualify-platforms.yml']:
  if not (ROOT/file).exists():continue
  current=None
  for line in text(file).split('\njobs:\n',1)[1].splitlines():
   if line.startswith('  ') and not line.startswith('   ') and line.endswith(':'):
    current=line.strip()[:-1];data[current]=[]
   elif current:data[current].append(line)
 return {key:'\n'.join(value) for key,value in data.items()}

class CredentialBoundary(unittest.TestCase):
 def test_only_final_promoter_has_contents_write(self):
  jobs=job_blocks();writers={name for name,body in jobs.items() if 'contents: write' in body}
  self.assertEqual(writers,{'promote'})
 def test_candidate_execution_has_no_token_environment(self):
  action=text('.github/actions/qualify-platform/action.yml')
  self.assertNotIn('GH_TOKEN:',action);self.assertNotIn('GITHUB_TOKEN:',action)
  self.assertIn('persist-credentials: false',text('.github/workflows/qualify-private-draft.yml'))
 def test_installed_candidate_cannot_see_parent_token(self):
  with tempfile.TemporaryDirectory() as directory:
   d=Path(directory);candidate=d/'assets';candidate.mkdir();tools=d/'tools';tools.mkdir();record=d/'observed.json'
   host=("#!"+sys.executable+"\nimport os,json,pathlib\npathlib.Path("+repr(str(record))+").write_text(json.dumps({'credential':bool(os.environ.get('GH_TOKEN') or os.environ.get('GITHUB_TOKEN'))}))\nprint('lmvideo 1.2.3')\n").encode()
   (candidate/'lmvideo-linux-x64').write_bytes(host)
   (candidate/'SHA256SUMS').write_text(hashlib.sha256(host).hexdigest()+'  lmvideo-linux-x64\n')
   (candidate/'RELEASE-MANIFEST.json').write_text(json.dumps({'tag':'v1.2.3','version':'1.2.3'}))
   compiler=tools/'compiler';compiler.write_text('#!'+sys.executable+"\nimport os,pathlib\nprint('source: PATH')\nprint('path: '+str(pathlib.Path(os.environ['RUNNER_TEMP'])/'lmvideo-draft-bin/lmvideo'))\n");compiler.chmod(0o755)
   env={'PATH':'/usr/bin:/bin','RUNNER_TEMP':str(d),'GITHUB_ENV':str(d/'env'),'GITHUB_PATH':str(d/'path'),'GH_TOKEN':'SYNTHETIC_ONLY','GITHUB_TOKEN':'SYNTHETIC_ONLY'}
   r=subprocess.run([sys.executable,str(ROOT/'scripts/install-draft-host.py'),'--compiler',str(compiler),'--tag','v1.2.3','--asset','lmvideo-linux-x64','--candidate',str(candidate)],env=env,capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr);self.assertFalse(json.loads(record.read_text())['credential'])
 def test_documented_permissions_match_workflow(self):
  doc=text('docs/release-pipeline.md');declared={}
  for line in doc.splitlines():
   if not line.startswith('|'):continue
   cells=[x.strip().strip('`') for x in line.strip('|').split('|')]
   if len(cells)>=2 and cells[0] in job_blocks():declared[cells[0]]=cells[1]
  actual={name:('contents:write' if 'contents: write' in body else 'contents:read')+('; actions:read' if 'actions: read' in body else '') for name,body in job_blocks().items()}
  self.assertEqual(declared,actual)
 def test_cross_run_artifacts_are_flattened(self):
  for file in ['.github/workflows/qualify-platforms.yml','.github/actions/qualify-platform/action.yml']:
   source=text(file)
   self.assertIn('artifact-ids:',source)
   self.assertIn('merge-multiple: true',source,file)
 def test_actions_pinned(self):
  for f in ['.github/workflows/qualify-private-draft.yml','.github/workflows/public-ci.yml','.github/workflows/qualify-platforms.yml','.github/actions/qualify-platform/action.yml']:
   for line in text(f).splitlines():
    if 'uses:' not in line:continue
    ref=line.split('uses:',1)[1].split('#',1)[0].strip()
    if ref.startswith('./'):continue
    pin=ref.rsplit('@',1)[-1]
    self.assertEqual(len(pin),40,(f,ref));self.assertTrue(all(c in '0123456789abcdef' for c in pin))
 def test_public_content_check_uses_compiler_with_run_lmcode(self):
  self.assertIn('@lmctl-ai/lmctl@0.2.458',text('.github/workflows/public-ci.yml'))
 def test_public_content_primes_installed_host_schemas_without_credentials(self):
  workflow=text('.github/workflows/public-ci.yml')
  self.assertIn('env -u GITHUB_TOKEN -u GH_TOKEN lmscript -e',workflow)
  for cli in ['lmvideo','lmcad','lmoffice pptx']:
   self.assertIn(f'load_schema(cli="{cli}")',workflow)
 def test_compact_pptx_example_uses_string_svg_input(self):
  source=text('examples/svg-compact-pptx.lms')
  self.assertNotIn('_xml_',source)
  self.assertIn('svg_text = "<svg ',source)
  self.assertIn('os.environ.get("LMVIDEO_QUALIFY_TAG", "v0.1.122")',text('tests/svg-compact-pptx.py'))
 def test_cancel_guard(self):
  self.assertIn('promote',job_blocks());body=job_blocks()['promote'];line=next(l for l in body.splitlines() if l.strip().startswith('if:'))
  self.assertIn('!cancelled()',line)
 def test_candidate_queue_does_not_drop_other_versions(self):
  workflow=text('.github/workflows/qualify-private-draft.yml')
  self.assertTrue(any(l.strip().startswith('group:') for l in workflow.splitlines()));group=next(l for l in workflow.splitlines() if l.strip().startswith('group:'))
  self.assertIn('client_payload.tag',group)
  self.assertIn('queue: max',job_blocks()['promote'])
 def test_only_the_verified_candidate_pin_can_bypass_published_tag_lookup(self):
  source=text('scripts/install-hosts.sh')
  self.assertIn('LMVIDEO_QUALIFY_TAG',source)
  self.assertIn('"$requested_tag" == "$LMVIDEO_QUALIFY_TAG"',source)
  self.assertIn('"$full_repo" == lmctl-ai/lmvideo',source)
 def test_native_executable_bits_are_restored_only_after_hash_validation(self):
  source=text('scripts/check-local-candidate.py')
  self.assertLess(source.index('validate_files(root)'),source.index('binary.chmod'))
  self.assertIn('0o111',source)
  self.assertIn('root / name',source)
 def test_hashed_legal_payloads_keep_committed_bytes_on_windows(self):
  attributes=text('.gitattributes').splitlines()
  required={'packaging/public-templates/lmvideo/LICENSE -text',
            'packaging/public-templates/lmvideo/LICENSE-examples -text',
            'packaging/public-templates/lmvideo/THIRD_PARTY_NOTICES.md -text',
            'packaging/public-templates/lmvideo/docs/third-party-licenses/** -text',
            'schema/lmvideo.dsl.json -text'}
  self.assertTrue(required.issubset(set(attributes)),attributes)
 def test_public_stage_text_uses_utf8_not_windows_locale(self):
  source=text('scripts/stage-public-content.py')
  self.assertIn('original = read_utf8(path)',source)
  stage=runpy.run_path(str(ROOT/'scripts/stage-public-content.py'),run_name='stage_public_content_test')
  with tempfile.TemporaryDirectory() as temporary:
   sample=Path(temporary)/'unicode.lms';sample.write_bytes('München — café\n'.encode('utf-8'))
   self.assertEqual(stage['read_utf8'](sample),'München — café\n')
 def test_fresh_installer_path_excludes_product_binary_directories(self):
  source=text('scripts/check-local-release.py')
  self.assertIn('path_tail=without_installed_host(os.environ.get("PATH",""),windows=os.name=="nt")',source)
  check=runpy.run_path(str(ROOT/'scripts/check-local-release.py'),run_name='check_local_release_test')
  with tempfile.TemporaryDirectory() as temporary:
   root=Path(temporary);candidate=root/'candidate';legacy=root/'legacy';choco=root/'chocolatey'/'bin';system=root/'Windows'/'System32'
   for directory in (candidate,legacy,choco,system):directory.mkdir(parents=True)
   (candidate/'lmvideo.exe').write_bytes(b'candidate')
   (legacy/'lmvideo.cmd').write_text('@echo off\n')
   (choco/'ffmpeg.exe').write_bytes(b'Chocolatey shim')
   path=os.pathsep.join(str(p) for p in (candidate,legacy,choco,system))
   kept=check['without_installed_host'](path,windows=True).split(os.pathsep)
   self.assertEqual(kept,[str(choco),str(system)])
 def test_windows_external_tools_use_original_runner_shims(self):
  check=runpy.run_path(str(ROOT/'scripts/check-local-release.py'),run_name='check_external_tools_test')
  with tempfile.TemporaryDirectory() as temporary:
   root=Path(temporary);original=root/'original';tools=root/'tools';original.mkdir();tools.mkdir()
   for name in ('ffmpeg','ffprobe'):
    binary=original/name;binary.write_text('#!/bin/sh\nexit 0\n');binary.chmod(0o755)
   check['alias_external_video_tools'](tools,str(original),windows=True)
   for name in ('ffmpeg','ffprobe'):
    self.assertFalse((tools/(name+'.exe')).exists(),name)
 def test_parent_handoff_matches_lmvideo_artifact_identity(self):
  source=text('scripts/verify-parent-handoff.py')
  self.assertIn("f'lmvideo-draft-{parent}-{attempt}'",source)
  self.assertNotIn('lmoffice-draft-',source)
 def test_svg_compact_installs_office_in_a_private_cache(self):
  source=text('tests/svg-compact-pptx.py')
  self.assertIn('private_home = private / "home"',source)
  self.assertIn('host_path).startswith(str(work)',source)
  self.assertIn('"lmoffice.exe"',source)
  self.assertIn('directory.resolve() == candidate_path.parent.resolve()',source)
 def test_windows_svg_compiler_launches_use_cmd_shims(self):
  for filename in ['tests/svg-dry-validation.py','tests/svg-compact-pptx.py']:
   source=text(filename)
   self.assertIn('cmd.exe',source)
   self.assertNotIn('cygpath',source)
 def test_replay_state_has_no_copied_pilot_records(self):
  revoked_text=text('REVOKED')
  self.assertNotIn('v0.1.59',revoked_text)
  self.assertNotIn('v0.1.60',revoked_text)
  state=json.loads(text('release-state.json'))
  candidates=state.get('candidates',{})
  self.assertNotIn('v0.1.59',candidates)
  self.assertNotIn('v0.1.60',candidates)
  for tag,entry in candidates.items():
   self.assertTrue(entry['private_run'].startswith('https://github.com/lmctlhq/lmvideo/'),tag)
   self.assertTrue(entry['public_run'].startswith('https://github.com/lmctl-ai/lmvideo/'),tag)
   self.assertTrue(entry['qualification_run'].startswith('https://github.com/lmctl-ai/lmvideo/'),tag)
 def test_all_always_predicates_are_cancel_safe(self):
  files=['.github/workflows/qualify-private-draft.yml','.github/workflows/qualify-platforms.yml','.github/workflows/integration.yml','.github/workflows/public-ci.yml','.github/actions/qualify-platform/action.yml']
  for f in files:
   for line in text(f).splitlines():
    if line.strip().startswith('if:') and 'always()' in line:
     self.assertIn('!cancelled()',line,(f,line))
 def test_qualify_platforms_passes_qualify_tag_and_pins_peer_tools(self):
  blocks=job_blocks()
  self.assertNotIn('needs.candidate',blocks['candidate'])
  self.assertIn('LMVIDEO_QUALIFY_TAG: ${{ needs.candidate.outputs.tag }}',blocks['qualify-macos-arm64'])
  self.assertIn('LMVIDEO_QUALIFY_TAG: ${{ needs.candidate.outputs.tag }}',blocks['integration'])
  self.assertIn('needs: [candidate, qualify-macos-arm64, integration]',blocks['deliberate-failure-test'])
  self.assertIn("needs.qualify-macos-arm64.result == 'success'",blocks['deliberate-failure-test'])
  for wf in ['.github/workflows/qualify-platforms.yml','.github/workflows/integration.yml']:
   s=text(wf)
   self.assertNotIn('choco install ffmpeg',s,wf)
   self.assertIn('scripts/install-windows-ffmpeg.py',s,wf)
  self.assertTrue((ROOT/'scripts/install-windows-ffmpeg.py').is_file())
  self.assertTrue((ROOT/'scripts/windows-ffmpeg.lock.json').is_file())
  hosts=text('scripts/install-hosts.sh')
  self.assertIn('--pattern SHA256SUMS',hosts)
  self.assertIn('PINNED_HOST_SHA256',hosts)
  self.assertNotIn('\n  tag="$latest_tag"',hosts)
 def test_runbook_and_workflow_hygiene(self):
  doc=text('docs/release-pipeline.md')
  self.assertIn('REVOKED',doc)
  self.assertIn('gh release edit',doc)
  self.assertIn('gh run rerun',doc)
  self.assertIn('qualify-private-draft.yml -f tag=',doc)
  readme=text('README.md')
  self.assertIn('docs/release-pipeline.md',readme)
  self.assertIn('FROZEN-CANDIDATE.json',readme)
  self.assertIn('PROVENANCE.jsonl',readme)
  self.assertIn('@lmctl-ai/lmctl@0.2.460',readme)
  self.assertNotIn('@lmctl-ai/lmctl@0.2.456',readme)
  integ=text('.github/workflows/integration.yml')
  self.assertIn("default: '0.2.460'",integ)
  self.assertNotIn('@lmctl-ai/lmctl${{',integ)
  self.assertNotIn('GITHUB_TOKEN: ${{ github.token }}',integ)
  pub_ci=text('.github/workflows/public-ci.yml')
  self.assertNotIn('GITHUB_TOKEN: ${{ github.token }}',pub_ci)
  self.assertFalse((ROOT/'scripts/__pycache__/integration-run.cpython-310.pyc').exists())

class SealedPolicy(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.path=ROOT/'scripts/release_policy.py'
  cls.module=runpy.run_path(str(cls.path)) if cls.path.exists() else {}
 def required(self,name):
  self.assertIn(name,self.module,'missing reusable release policy '+name)
  return self.module[name]
 def test_provenance_required(self):
  validate=self.required('validate_manifest')
  with self.assertRaises(ValueError):validate({'format':'lmvideo-release-candidate/1','repository':'lmctl-ai/lmvideo','source_sha':'0'*40})
 def test_older_and_revoked_refused(self):
  check=self.required('allow_promotion')
  with self.assertRaises(ValueError):check('v0.1.20','v0.1.58',[])
  with self.assertRaises(ValueError):check('v0.1.59','v0.1.58',['v0.1.59'])
  revoked=[l.split()[0] for l in text('REVOKED').splitlines() if l.strip() and not l.startswith('#')]
  state=json.loads(text('release-state.json'))
  published=[k for k,v in state.get('candidates',{}).items() if v.get('outcome')=='published' and v.get('public_run','').startswith('https://github.com/lmctl-ai/lmvideo/')]
  latest=max(published,key=self.required('version'))
  self.assertIn('v0.1.121',revoked)
  with self.assertRaises(ValueError):check('v0.1.121',latest,revoked)
  with self.assertRaises(ValueError):check('v0.1.107',latest,revoked)
 def test_cleanup_requires_sealed_throwaway(self):
  check=self.required('allow_cleanup')
  with self.assertRaises(ValueError):check({'deliberate_failure':False})
 def test_changed_asset_identity_refused(self):
  check=self.required('same_assets')
  with self.assertRaises(ValueError):check([{'id':1,'name':'a','digest':'sha256:aa','size':2}],[{'id':2,'name':'a','digest':'sha256:aa','size':2}])
 def test_private_attestation_verifier_is_required(self):
  verify=text('scripts/verify-draft-candidate.py')
  self.assertIn('verify_attestation',verify)

if __name__=='__main__':unittest.main()
