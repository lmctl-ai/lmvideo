import contextlib,copy,io,json,os,runpy,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
from release_fixtures import ROOT,MemoryAPI,candidate,policy

class ReleaseSecurity(unittest.TestCase):
 def run_manager(self,root,request,api,mode='promote'):
  module=runpy.run_path(str(ROOT/'scripts/manage-qualified-draft.py'));main=module['main'];g=main.__globals__;g['API']=lambda:api
  # Only cryptographic verification is stubbed; all structural/byte and API-state policies are real.
  g['verify_attestation']=policy.validate_files
  env={'DRAFT_TAG':request['tag'],'SEAL_ASSET_ID':str(request['seal_asset_id']),'BUNDLE_ASSET_ID':str(request['bundle_asset_id']),'FREEZE_SHA':request['seal_sha256']}
  with patch.dict(os.environ,env),patch.object(sys,'argv',['manager',mode,str(root)]),contextlib.redirect_stdout(io.StringIO()):main()
 def fixture(self,**kwargs):
  temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup);root=Path(temporary.name);m,seal,r=candidate(root,**kwargs);return root,m,seal,r
 def test_normal_publication_requires_locked_assets_before_latest(self):
  root,m,seal,r=self.fixture();api=MemoryAPI(root,seal,r);self.run_manager(root,r,api)
  self.assertEqual(api.patches,[{'draft':False,'make_latest':'false'},{'make_latest':'true'}]);self.assertTrue(api.release['immutable'])
 def test_older_candidate_does_not_patch(self):
  root,m,seal,r=self.fixture();api=MemoryAPI(root,seal,r,latest='v2.0.0')
  with self.assertRaises(ValueError):self.run_manager(root,r,api)
  self.assertEqual(api.patches,[])
 def test_revoked_replay_does_not_patch(self):
  root,m,seal,r=self.fixture();api=MemoryAPI(root,seal,r,revoked=m['tag']+' operator rollback\n')
  with self.assertRaises(ValueError):self.run_manager(root,r,api)
  self.assertEqual(api.patches,[])
 def test_cleanup_cannot_be_selected_for_production_candidate(self):
  root,m,seal,r=self.fixture();api=MemoryAPI(root,seal,r)
  with self.assertRaises(ValueError):self.run_manager(root,r,api,'cleanup')
  self.assertEqual(api.deleted,[])
 def test_sealed_throwaway_revokes_before_deletion(self):
  root,m,seal,r=self.fixture(throwaway=True);api=MemoryAPI(root,seal,r);self.run_manager(root,r,api,'cleanup')
  self.assertIn(m['tag'],api.files['REVOKED'].decode());self.assertEqual(api.deleted,[77]);self.assertFalse(api.patches)
 def test_throwaway_can_never_be_promoted(self):
  root,m,seal,r=self.fixture(throwaway=True);api=MemoryAPI(root,seal,r)
  with self.assertRaises(ValueError):self.run_manager(root,r,api)
  self.assertFalse(api.patches)
 def test_asset_replacement_after_download_is_caught(self):
  root,m,seal,r=self.fixture();api=MemoryAPI(root,seal,r,race=True)
  with self.assertRaises(ValueError):self.run_manager(root,r,api)
  self.assertFalse(api.patches)
 def test_same_current_published_replay_is_noop(self):
  root,m,seal,r=self.fixture();api=MemoryAPI(root,seal,r,draft=False);api.latest=api.release;self.run_manager(root,r,api);self.assertFalse(api.patches)
 def test_metadata_hash_and_private_api_claims_are_required(self):
  root,m,seal,r=self.fixture();policy.validate_files(root)
  for key,value in [('private_repository','attacker/repo'),('workflow','other.yml'),('branch','evil'),('source_sha','0'*40),('run_id','999'),('run_attempt','2'),('deliberate_failure','true')]:
   bad=copy.deepcopy(m);bad[key]=value
   with self.assertRaises(ValueError,msg=key):policy.validate_manifest(bad)
 def test_failed_or_missing_private_gate_refused(self):
  root,m,seal,r=self.fixture();m['private_run']['jobs'][0]['conclusion']='failure'
  with self.assertRaises(ValueError):policy.validate_manifest(m)
 def test_real_crypto_verifier_fail_closed_and_enforces_run_certificate(self):
  root,m,seal,r=self.fixture()
  class Result:
   returncode=0;stdout=b'[]'
  calls=[]
  def execute(args,**kwargs):calls.append(args);return Result()
  with patch.object(policy.subprocess,'run',execute):
   with self.assertRaises(ValueError):policy.verify_attestation(root)
  self.assertIn('--source-digest',calls[0]);self.assertIn('--no-public-good',calls[0]);self.assertIn('--source-ref',calls[0]);self.assertIn('--signer-workflow',calls[0]);self.assertIn('--deny-self-hosted-runners',calls[0])
  Result.returncode=1
  with patch.object(policy.subprocess,'run',execute):
   with self.assertRaises(ValueError):policy.verify_attestation(root)
  Result.returncode=0;Result.stdout=json.dumps([{'verificationResult':{'signature':{'certificate':{'runInvocationURI':'https://github.com/'+policy.PRIVATE+'/actions/runs/999/attempts/1'}}}}]).encode()
  with patch.object(policy.subprocess,'run',execute):
   with self.assertRaises(ValueError):policy.verify_attestation(root)
  Result.stdout=json.dumps([{'verificationResult':{'signature':{'certificate':{'runInvocationURI':'https://github.com/'+policy.PRIVATE+'/actions/runs/123/attempts/2'}}}}]).encode()
  with patch.object(policy.subprocess,'run',execute):self.assertEqual(policy.verify_attestation(root)[0]['tag'],m['tag'])
 def test_cancellation_expression_uses_actual_workflow(self):
  workflow=(ROOT/'.github/workflows/qualify-private-draft.yml').read_text() if (ROOT/'.github/workflows/qualify-private-draft.yml').exists() else '';self.assertIn('  promote:',workflow);body=workflow.split('  promote:',1)[1]
  expr=next(l for l in body.splitlines() if l.strip().startswith('if:')).split('${{',1)[1].split('}}',1)[0]
  values={'needs.regression.result':'success','needs.candidate.result':'success','needs.qualify-macos-arm64.result':'success','needs.integration.result':'success','needs.deliberate-failure-test.result':'skipped','needs.candidate.outputs.failure_control':'false'}
  def evaluate(cancelled):
   result=expr.replace('!cancelled()',str(not cancelled)).replace('always()','True')
   for key,value in values.items():result=result.replace(key,repr(value))
   return eval(result.replace('&&',' and ').replace('||',' or '),{'__builtins__':{}},{})
  self.assertTrue(evaluate(False));self.assertFalse(evaluate(True))
  values['needs.regression.result']='failure';self.assertFalse(evaluate(False))


class ProducerFreeze(unittest.TestCase):
 def test_retry_never_repairs_a_missing_frozen_asset(self):
  module=runpy.run_path(str(ROOT/'scripts/stage-frozen-draft.py'));upload=module['upload']
  with tempfile.TemporaryDirectory() as directory:
   p=Path(directory)/'lmvideo-linux-x64';p.write_bytes(b'payload')
   release={'tag_name':'v1.2.3','assets':[{'name':policy.SEAL,'id':99}]}
   class NoWrite:
    def request(self,*args,**kwargs):raise AssertionError('no upload/lookup allowed')
   with patch.object(module['subprocess'],'run',side_effect=AssertionError('network upload forbidden')):
    with self.assertRaises(ValueError):upload(NoWrite(),release,p.name,p)
 def test_existing_matching_asset_is_verified_without_upload(self):
  module=runpy.run_path(str(ROOT/'scripts/stage-frozen-draft.py'));upload=module['upload']
  with tempfile.TemporaryDirectory() as directory:
   p=Path(directory)/'lmvideo-linux-x64';p.write_bytes(b'payload');a={'name':p.name,'id':1,'digest':'sha256:'+policy.digest(b'payload'),'size':7}
   class ReadOnly:
    def asset(self,identifier):return b'payload'
   with patch.object(module['subprocess'],'run',side_effect=AssertionError('upload forbidden')):self.assertEqual(upload(ReadOnly(),{'assets':[a]},p.name,p),a)
 def test_changed_existing_asset_is_refused_without_upload(self):
  module=runpy.run_path(str(ROOT/'scripts/stage-frozen-draft.py'));upload=module['upload']
  with tempfile.TemporaryDirectory() as directory:
   p=Path(directory)/'lmvideo-linux-x64';p.write_bytes(b'payload');a={'name':p.name,'id':1,'digest':'sha256:'+policy.digest(b'changed'),'size':7}
   with self.assertRaises(ValueError):upload(object(),{'assets':[a]},p.name,p)

if __name__=='__main__':unittest.main()

class NativeAPIGates(unittest.TestCase):
 def test_all_native_legs_and_exact_workflow_source_are_required(self):
  module=runpy.run_path(str(ROOT/'scripts/qualification_broker.py'));validate=module['validate_qualification']
  run={'path':'.github/workflows/qualify-platforms.yml','head_branch':'main','head_sha':'a'*40,'event':'repository_dispatch','status':'completed','conclusion':'success'}
  jobs=[{'name':n,'status':'completed','conclusion':'success'} for n in ['candidate','Qualify macos-arm64','Qualify macos-x64','Qualify linux-x64','Qualify linux-arm64','Qualify windows-x64']]+[{'name':'deliberate-failure-test','status':'completed','conclusion':'skipped'}]
  validate(run,jobs,'a'*40,False)
  for index in range(6):
   bad=copy.deepcopy(jobs);bad[index]['conclusion']='failure'
   with self.assertRaises(ValueError,msg=jobs[index]['name']):validate(run,bad,'a'*40,False)
  for key,value in [('head_sha','b'*40),('head_branch','fork'),('event','workflow_dispatch'),('path','other.yml'),('status','in_progress')]:
   bad=dict(run);bad[key]=value
   with self.assertRaises(ValueError,msg=key):validate(bad,jobs,'a'*40,False)
 def test_throwaway_only_accepts_the_expected_failure(self):
  module=runpy.run_path(str(ROOT/'scripts/qualification_broker.py'));validate=module['validate_qualification']
  run={'path':'.github/workflows/qualify-platforms.yml','head_branch':'main','head_sha':'a'*40,'event':'repository_dispatch','status':'completed','conclusion':'failure'}
  jobs=[{'name':n,'status':'completed','conclusion':'success'} for n in ['candidate','Qualify macos-arm64','Qualify macos-x64','Qualify linux-x64','Qualify linux-arm64','Qualify windows-x64']]+[{'name':'deliberate-failure-test','status':'completed','conclusion':'failure'}]
  validate(run,jobs,'a'*40,True)
  with self.assertRaises(ValueError):validate(run,jobs,'a'*40,False)
  jobs[-1]['conclusion']='skipped'
  with self.assertRaises(ValueError):validate(run,jobs,'a'*40,True)

 def test_throwaway_cleanup_never_runs_on_native_or_foreign_failure(self):
  source=(ROOT/'scripts/qualification_broker.py').read_text(encoding='utf-8')
  self.assertNotIn('cleanup_failed_throwaway',source)
  module=runpy.run_path(str(ROOT/'scripts/qualification_broker.py'))
  self.assertNotIn('cleanup_failed_throwaway',module)
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);manifest,seal,request=candidate(root,throwaway=True)
   base_run={'id':456,'path':'.github/workflows/qualify-platforms.yml','head_branch':'main','head_sha':'a'*40,'event':'repository_dispatch','status':'completed','conclusion':'failure','display_title':f"Qualify artifact 111.1 {manifest['tag']}"}
   base_jobs=[{'name':n,'status':'completed','conclusion':'success'} for n in ['candidate','Qualify macos-arm64','Qualify macos-x64','Qualify linux-x64','Qualify linux-arm64','Qualify windows-x64']]+[{'name':'deliberate-failure-test','status':'completed','conclusion':'failure'}]
   def run_broker(run_obj,jobs_list):
    calls=[]
    class FakeAPI:
     def request(self,path,method='GET',data=None):
      if path=='dispatches':return {}
      if path.startswith('actions/workflows/qualify-platforms.yml/runs'):return {'workflow_runs':[run_obj]}
      if path==f'actions/runs/{run_obj["id"]}':return run_obj
      if path.startswith(f'actions/runs/{run_obj["id"]}/jobs'):return {'jobs':jobs_list}
      raise AssertionError('unexpected API path '+path)
    class FakeSubprocess:
     @staticmethod
     def run(args,env=None,check=False):
      calls.append((args,env,check))
      class R:returncode=1
      return R()
    env={'GITHUB_RUN_ID':'111','GITHUB_RUN_ATTEMPT':'1','ARTIFACT_ID':'222','ARTIFACT_DIGEST':'sha256:'+'b'*64,'GITHUB_SHA':'a'*40}
    with patch.dict(os.environ,env),patch.dict(module['broker'].__globals__,{'API':FakeAPI,'Path':lambda p:root if p=='draft-assets' else Path(p),'validate_files':lambda r:(manifest,seal),'subprocess':FakeSubprocess,'summary':lambda msg:None}):
     module['broker']()
    return calls
   # 1. Native platform failure on a throwaway run MUST raise ValueError and NEVER invoke cleanup.
   native_fail_jobs=copy.deepcopy(base_jobs)
   native_fail_jobs[1]['conclusion']='failure'
   native_fail_jobs[-1]['conclusion']='skipped'
   with self.assertRaises(ValueError):
    run_broker(base_run,native_fail_jobs)
   # 2. Incomplete deliberate-failure-test job MUST raise ValueError and NEVER invoke cleanup.
   incomplete_control_jobs=copy.deepcopy(base_jobs)
   incomplete_control_jobs[-1]['status']='in_progress'
   with self.assertRaises(ValueError):
    run_broker(base_run,incomplete_control_jobs)
   # 3. Only when all 5 native platform gates pass and deliberate-failure-test fails does cleanup run.
   with self.assertRaises(SystemExit):
    run_broker(base_run,base_jobs)

class ProducerInputBinding(unittest.TestCase):
 def test_private_receipt_cannot_change_the_selected_tag_or_throwaway_input(self):
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);m,seal,request=candidate(root,throwaway=True)
   class PrivateAPI:
    def request(self,path):
     if '/jobs?' in path:return {'jobs':m['private_run']['jobs']}
     return {'id':123,'run_attempt':1,'head_sha':'a'*40,'head_branch':'main','path':'.github/workflows/release.yml','event':'workflow_dispatch'}
   for expected_tag,purpose in [(m['tag'],'false'),('v9.9.9','true')]:
    env={'GITHUB_SHA':'a'*40,'GITHUB_RUN_ID':'123','EXPECTED_TAG':expected_tag,'DELIBERATE_FAILURE':purpose}
    with patch.dict(os.environ,env),patch.object(sys,'argv',['verify',str(root)]),patch.object(policy,'API',lambda repo:PrivateAPI()):
     with self.assertRaises((ValueError,SystemExit)):
      runpy.run_path(str(ROOT/'scripts/verify-private-build.py'))
