# lmvideo private draft and public qualification

A release starts only from a manual `workflow_dispatch` of the private
`lmctlhq/lmvideo` `release.yml` workflow on `main`. Its patch is the committed
`VERSION` floor plus that workflow run number. Dispatching one `only_os` target
is diagnostic and cannot publish. An `all` dispatch builds the five shipped
native targets, performs a credential-free smoke and creates a public draft.
The draft is not a release until the public workflow qualifies the exact bytes.

## Authority and execution boundary

| Job | Permissions | Candidate execution |
| --- | --- | --- |
| regression | contents:read | Synthetic offline release controls only |
| promote | contents:write; actions:read | Never; trusted draft broker and final promotion/cleanup |
| candidate | contents:read; actions:read | No; validates parent run and frozen handoff |
| qualify-macos-arm64 | contents:read; actions:read | Yes, without publication credentials |
| integration | contents:read; actions:read | Yes, on macOS x64, Linux x64/ARM64 and Windows x64 |
| deliberate-failure-test | contents:read | No; sealed throwaway-only failure control |

The public `promote` job is the only release writer. It fetches draft assets by
sealed IDs, verifies the private run attestation, freezes those bytes in an
Actions artifact, asks `qualify-platforms.yml` to test them with read-only jobs,
and checks the child workflow's exact identity and every native result before
changing the draft. The candidate runs only in those read-only jobs. Checkouts
in the release path disable persisted credentials. `RELEASE_TOKEN` is used only
by the private publisher to stage the public draft and send its dispatch.

The candidate manifest binds the private repository, workflow, branch, source
SHA, run ID, attempt, all prerequisite jobs and the boolean throwaway purpose.
The publisher checks the original workflow run through its read-only Actions
API, signs a frozen release/asset identity with a GitHub provenance attestation,
and dispatches only asset IDs and digests. Retries may reuse matching frozen
bytes; they cannot clobber a changed candidate. Public verification checks live
revocations, release freshness and all asset identities before promotion. A
throwaway failure is revoked before its own draft release object is removed.
Artifact ZIPs do not preserve POSIX executable bits; the read-only worker
revalidates sealed bytes before restoring the executable mode in its private
workspace.

## Public gates

Each native worker verifies the same frozen asset set and uses a private runner
prefix for lmscript. It installs the exact candidate from its isolated PATH and
runs the lmcode framing/result controls, SVG dry validation, CPU rigid-body API
and refusal checks, the `svg-compact` PNG-to-PowerPoint example, fresh-installer
and raster/Cycles compatibility checks, and every public example on that OS.
The independent macOS ARM64 gate runs before the other four platform jobs.
Candidate processes receive no GitHub token or persisted checkout credential.

The separately maintained `.github/workflows/physics.yml` is diagnostic and is
not a release dependency. The release qualification includes the CPU rigid-body
API gate and public physics examples; it does not require a GPU.

## Observe and recover

Dispatch a production release or a sealed throwaway failure-path check from private `main`:

```sh
gh workflow run release.yml -R lmctlhq/lmvideo --ref main -f only_os=all -f deliberate_public_failure=false
gh workflow run release.yml -R lmctlhq/lmvideo --ref main -f only_os=all -f deliberate_public_failure=true
gh run list -R lmctlhq/lmvideo --workflow release.yml --limit 5
gh run list -R lmctl-ai/lmvideo --workflow qualify-private-draft.yml --limit 10
gh run list -R lmctl-ai/lmvideo --workflow qualify-platforms.yml --limit 10
```

For one native diagnostic, choose one of `linux-x64`, `linux-arm64`,
`macos-x64`, `macos-arm64`, or `windows-x64`; that run does not publish. For a
throwaway failure-path run, set `deliberate_public_failure=true` before
candidate sealing. Public failure keeps an ordinary draft unpublished; only
the sealed throwaway control is eligible for cleanup, and only when all five
native platform gates pass and the `deliberate-failure-test` job fails.

If the private build succeeded and only the public handoff or runner queue
failed transiently, redrive the original private `publish-draft` job so it
reuses the original build attempt and frozen asset IDs, or replay a recorded
tag in the public broker:

```sh
gh run rerun <PRIVATE_RUN_ID> -R lmctlhq/lmvideo --failed
gh workflow run qualify-private-draft.yml -f tag=v0.1.<N> -R lmctl-ai/lmvideo --ref main
```

A source or build change requires a new private manual run and a new derived
version (`0.1.<100 + run_number>`).

### Operator rollback

If a published release must be rolled back, append the tag and reason to
`REVOKED` and push to `lmctl-ai/lmvideo` `main` **first** so any concurrent or
subsequent replay of that tag is refused immediately, then move `Latest` back
to the last known-good release:

```sh
printf 'v0.1.<BAD> operator rollback: <reason>\n' >> REVOKED
git add REVOKED && git commit -m "Revoke v0.1.<BAD>" && git push origin main
gh release edit v0.1.<GOOD> -R lmctl-ai/lmvideo --latest
```

The public release includes five native executables, `LICENSE`,
`THIRD_PARTY_NOTICES.md`, `lmvideo-third-party-licenses.tar.gz`,
`RELEASE-MANIFEST.json`, `SHA256SUMS`, `FROZEN-CANDIDATE.json`, and
`PROVENANCE.jsonl`. Verify downloaded assets with
`sha256sum --check SHA256SUMS` (or `shasum -a 256` on macOS).
