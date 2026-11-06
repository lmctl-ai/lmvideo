#!/usr/bin/env python3
"""Mutate an isolated source snapshot; the normal offline suite must go red."""
from pathlib import Path
import json,os,shutil,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
mutations=[
 ('credential-inheritance','scripts/release_policy.py',"return {k:v for k,v in env.items() if k not in {'GH_TOKEN','GITHUB_TOKEN','GH_ENTERPRISE_TOKEN','GITHUB_ENTERPRISE_TOKEN','ACTIONS_ID_TOKEN_REQUEST_TOKEN','ACTIONS_ID_TOKEN_REQUEST_URL'}}",'return dict(env)'),
 ('producer-identity','scripts/release_policy.py',"m.get('private_repository')!=PRIVATE","False"),
 ('certificate-run','scripts/release_policy.py',"if parts[4]==str(m['run_id']) and positive(parts[6])>=positive(m['run_attempt']):","if True:"),
 ('selected-tag','scripts/verify-private-build.py',"if m.get('tag')!=os.environ['EXPECTED_TAG']:","if False:"),
 ('selected-purpose','scripts/verify-private-build.py'," or m['deliberate_failure']!=(purpose=='true')",""),
 ('sealed-cleanup','scripts/release_policy.py',"if manifest.get('deliberate_failure') is not True:","if False:"),
 ('older-candidate','scripts/release_policy.py',"if latest and key<version(latest):","if False:"),
 ('revoked-candidate','scripts/release_policy.py',"if tag in revoked:","if False:"),
 ('asset-id','scripts/release_policy.py',"'id':positive(asset['id'])","'id':1"),
 ('late-recheck','scripts/manage-qualified-draft.py',"policy(api,tag);release,etag=recheck(api,root,seal,request,False)","policy(api,tag)"),
 ('frozen-repair','scripts/stage-frozen-draft.py',"if any(a['name'] in {SEAL,BUNDLE} for a in release.get('assets',[])) and name!=BUNDLE:","if False:"),
 ('native-api-gate','scripts/qualification_broker.py',"for name in {'candidate'}|PLATFORMS:","for name in {'candidate'}:"),
 ('cancel',' .github/workflows/qualify-private-draft.yml'.strip(),"!cancelled() && ",""),
 ('queue-candidate','.github/workflows/qualify-private-draft.yml',"group: lmvideo-qualification-${{ inputs.tag || github.event.client_payload.tag }}","group: one-shared-pending-slot"),
 ('queue-writer','.github/workflows/qualify-private-draft.yml',"      queue: max\n",""),
 ('artifact-layout','.github/actions/qualify-platform/action.yml','merge-multiple: true','merge-multiple: false'),
 ('action-pin','.github/workflows/qualify-private-draft.yml','actions/checkout@11d5960a326750d5838078e36cf38b85af677262','actions/checkout@v4'),
 ('candidate-example-pin','scripts/install-hosts.sh','"$full_repo" == lmctl-ai/lmvideo && -n "${LMVIDEO_QUALIFY_TAG:-}" && "$requested_tag" == "$LMVIDEO_QUALIFY_TAG"','false'),
 ('parent-artifact-name','scripts/verify-parent-handoff.py',"f'lmvideo-draft-{parent}-{attempt}'","f'lmoffice-draft-{parent}-{attempt}'"),
 ('throwaway-failed-gate-cleanup','scripts/qualification_broker.py',"validate_qualification(run,jobs(api,run_id),os.environ['GITHUB_SHA'],manifest['deliberate_failure'])","None"),
 ('copied-pilot-revoked','REVOKED','v0.1.121 sealed throwaway qualification failure','v0.1.59 deliberate_public_failure qualification test\nv0.1.121 sealed throwaway qualification failure'),
 ('copied-pilot-state','release-state.json','https://github.com/lmctl-ai/lmvideo/actions/runs/','https://github.com/lmctl-ai/lmoffice/actions/runs/'),
 ('action-cancel','.github/actions/qualify-platform/action.yml','if: always() && !cancelled()','if: always()'),
 ('deliberate-failure-needs-macos-arm64','.github/workflows/qualify-platforms.yml','needs: [candidate, qualify-macos-arm64, integration]','needs: [candidate, integration]'),
 ('qualify-tag-integration','.github/workflows/qualify-platforms.yml','          LMVIDEO_QUALIFY_TAG: ${{ needs.candidate.outputs.tag }}\n        run: |\n          set -euo pipefail\n          if [ "$RUNNER_OS" = Linux ]','        run: |\n          set -euo pipefail\n          if [ "$RUNNER_OS" = Linux ]'),
 ('peer-host-sha256','scripts/install-hosts.sh','--pattern SHA256SUMS',''),
 ('windows-ffmpeg-pin','.github/workflows/qualify-platforms.yml','python scripts/install-windows-ffmpeg.py --output-dir "$RUNNER_TEMP/lmvideo-ffmpeg"','choco install ffmpeg -y --no-progress'),
 ('runbook-rollback','docs/release-pipeline.md','gh release edit v0.1.<GOOD> -R lmctl-ai/lmvideo --latest',''),
 ('readme-runbook-link','README.md','docs/release-pipeline.md',''),
 ('integration-direct-interp','.github/workflows/integration.yml','@lmctl-ai/lmctl${LMCTL_VERSION:+@$LMCTL_VERSION}','@lmctl-ai/lmctl${{ inputs.lmctl_version }}'),
 ('compact-office-private-cache','tests/svg-compact-pptx.py','if any((directory / name).is_file() for name in ("lmvideo", "lmvideo.exe", "lmoffice", "lmoffice.exe")):', 'if False:'),
 ('windows-svg-compiler-launch','tests/svg-dry-validation.py','["cmd.exe", "/d", "/s", "/c", subprocess.list2cmdline(command)]','["bash", "/c", subprocess.list2cmdline(command)]'),
 ('candidate-mode-restore','scripts/check-local-candidate.py','binary.chmod(binary.stat().st_mode | 0o111)','binary.chmod(binary.stat().st_mode)'),
 ('legal-line-ending-checkout','.gitattributes','packaging/public-templates/lmvideo/docs/third-party-licenses/** -text',''),
 ('schema-line-ending-checkout','.gitattributes','schema/lmvideo.dsl.json -text',''),
 ('windows-default-text-encoding','scripts/stage-public-content.py','path.read_text(encoding="utf-8", errors="replace")','path.read_text(encoding="cp1252", errors="replace")'),
 ('windows-stage-default-encoding','scripts/stage-public-content.py','original = read_utf8(path)','original = path.read_text(errors="replace")'),
 ('windows-fresh-installer-path','scripts/check-local-release.py','return filtered if windows else (filtered or "/usr/bin:/bin")','return path_value'),
 ('windows-copy-chocolatey-shim','scripts/check-local-release.py','if windows:\n            # Keep Chocolatey shims beside their relative target; the source PATH is sanitized below.\n            selected[name] = str(binary)\n            continue','if windows:\n            shutil.copy2(binary, tools/(name+".exe"))\n            selected[name] = str(binary)\n            continue'),
 ('doc-permissions','docs/release-pipeline.md','| promote | contents:write; actions:read |','| promote | contents:read; actions:read |'),
]
with tempfile.TemporaryDirectory(prefix='release-mutations-') as temporary:
 root=Path(temporary)
 for name in ['scripts','tests','.github','docs']:
  shutil.copytree(ROOT/name,root/name,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
 shutil.copy2(ROOT/'release-policy.json',root/'release-policy.json')
 shutil.copy2(ROOT/'REVOKED',root/'REVOKED')
 shutil.copy2(ROOT/'release-state.json',root/'release-state.json')
 shutil.copy2(ROOT/'README.md',root/'README.md')
 shutil.copy2(ROOT/'.gitattributes',root/'.gitattributes')
 shutil.copy2(ROOT/'toolchain.lock.json',root/'toolchain.lock.json')
 (root/'examples').mkdir()
 shutil.copy2(ROOT/'examples/svg-compact-pptx.lms',root/'examples/svg-compact-pptx.lms')
 (root/'packaging').mkdir()
 shutil.copy2(ROOT/'packaging/release-examples.lock.json',root/'packaging/release-examples.lock.json')
 command=[sys.executable,'-B','-m','unittest','discover','-s','tests','-p','test_release*.py']
 baseline=subprocess.run(command,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 if baseline.returncode:sys.stdout.buffer.write(baseline.stdout);raise SystemExit('mutation baseline is not green')
 for name,file,old,new in mutations:
  p=root/file;source=p.read_text()
  if old not in source:raise SystemExit('mutation target not found: '+name)
  try:
   p.write_text(source.replace(old,new,1));r=subprocess.run(command,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
   if r.returncode==0:raise SystemExit('SURVIVED '+name)
   if b'ModuleNotFoundError' in r.stdout or b'SyntaxError' in r.stdout:sys.stdout.buffer.write(r.stdout);raise SystemExit('invalid control '+name)
   print('CAUGHT '+name,flush=True)
  finally:p.write_text(source)
print('PASS '+str(len(mutations))+' release mutations')
