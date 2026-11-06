#!/usr/bin/env bash
set -euo pipefail
shopt -s globstar nullglob

if [[ ! -d examples ]]; then
  echo "install-hosts: no examples directory; nothing to install"
  exit 0
fi

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) plat=linux-x64; ext="" ;;
  Linux-aarch64|Linux-arm64) plat=linux-arm64; ext="" ;;
  Darwin-x86_64) plat=macos-x64; ext="" ;;
  Darwin-arm64) plat=macos-arm64; ext="" ;;
  MINGW*|MSYS*|CYGWIN*) plat=windows-x64; ext=".exe" ;;
  *) echo "install-hosts: unsupported platform $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

host_cache="${XDG_CACHE_HOME:-${HOME:-.}/.cache}/lmctl"
runner_scratch="${RUNNER_TEMP:-${TMPDIR:-$host_cache/tmp}}"
host_bin="${LMCTL_HOST_BIN_DIR:-$host_cache/hosts}"
mkdir -p "$host_bin" "$runner_scratch"
skip_file="$runner_scratch/lmctl-unavailable-examples.tsv"
: > "$skip_file"

declare -A tags_by_repo=()
declare -A owner_by_repo=()
declare -A cli_names=()
declare -A cli_repo=()
declare -A unavailable_reason=()
declare -A resolved_latest=()
declare -A PINNED_HOST_SHA256=(
  ["lmctl-ai/lmcad@v0.1.5:lmcad-linux-arm64"]="d303235dd040c94331cd714380ddfbc0cfffed34130bd99785b1aab9e58465a0"
  ["lmctl-ai/lmcad@v0.1.5:lmcad-linux-x64"]="d30fd436b513c3b2dfefe84fe8b223420f2a05b33331bfdc472482656917387e"
  ["lmctl-ai/lmcad@v0.1.5:lmcad-macos-arm64"]="3a8923251b415132dbd082b8aa4206d66f806c741ade33b0e254056f7c55e03e"
  ["lmctl-ai/lmcad@v0.1.5:lmcad-macos-x64"]="a9a7091bd41d7012ecf9d2bb8093d279f22f29ed4948dc7a3bb26e952995ca64"
  ["lmctl-ai/lmcad@v0.1.5:lmcad-windows-x64.exe"]="3ff7348876dd32f247eef83078a4bc44fea9b95ff786bc8524071118254eb015"
  ["lmctl-ai/lmoffice@v0.1.34:lmoffice-linux-arm64"]="4f02cc436055a7a11eeceb1236c3e85dc5e5ea71ea454f76cfdd3510d06e24c2"
  ["lmctl-ai/lmoffice@v0.1.34:lmoffice-linux-x64"]="2b67e76f84bb267d77c5b01b832fc6be5da80ece716331fe525ff37b559f18b4"
  ["lmctl-ai/lmoffice@v0.1.34:lmoffice-macos-arm64"]="b99bcadfa538318050c797ea0b3f3738d3989d7e5387ec0fe21617e907a64e89"
  ["lmctl-ai/lmoffice@v0.1.34:lmoffice-macos-x64"]="f4f56677b3512d7a7a15f2a818d61825ddd7686df02b5dbd467bcbaaa00fb2c3"
  ["lmctl-ai/lmoffice@v0.1.34:lmoffice-windows-x64.exe"]="7d499ee96a13687d33b9efde4c442661f4762e64cf23e0583dd5ca127b9847f6"
  ["lmctl-ai/lmoffice@v0.1.58:lmoffice-linux-arm64"]="72d50e524d179ea5549f3cd5870d3b7e8b05bc8ad6bfe1e52b94403253ebf12c"
  ["lmctl-ai/lmoffice@v0.1.58:lmoffice-linux-x64"]="b1bb094b110456f11784064492074a5e9636787dd8965c93d02f99ea4fb4c478"
  ["lmctl-ai/lmoffice@v0.1.58:lmoffice-macos-arm64"]="ab212d3e6c435d27947015b7558ff6549945f447ee39331e1be4e91793a8023c"
  ["lmctl-ai/lmoffice@v0.1.58:lmoffice-macos-x64"]="7f79c90f131656bb27ab9fe40e6f9c8e87056ea6e8af1619d37b22cdcedc09d2"
  ["lmctl-ai/lmoffice@v0.1.58:lmoffice-windows-x64.exe"]="36bc61d36ea211b230b81e4fdc646d3853192c4f1e4915a4f6a31127ea379057"
  ["lmctl-ai/lmoffice@v0.1.60:lmoffice-linux-arm64"]="942f6924ac60c30db4fb7e83738704df97ba2960452168a72bbc640b1eab0ff5"
  ["lmctl-ai/lmoffice@v0.1.60:lmoffice-linux-x64"]="638c8c1c95b8fb433b0be44c66d46bad7bbe19d55c99bc52680fab1ad375611f"
  ["lmctl-ai/lmoffice@v0.1.60:lmoffice-macos-arm64"]="571dc587cf2fb4f8d01b8ec40f521d3e92d9e690d93533cd74772f6398254590"
  ["lmctl-ai/lmoffice@v0.1.60:lmoffice-macos-x64"]="a3b05dc71fbb05ecbc5b30e908b5f7bfdad141b7ac976ee168ce4c6bbfd27b89"
  ["lmctl-ai/lmoffice@v0.1.60:lmoffice-windows-x64.exe"]="39265f2389e6a2fcbd84c3139856c4bfd8dcab44cd1853f812ad09ca3ef3fc96"
)
install_re="install[[:space:]]*=[[:space:]]*[\"'](github:[^\"']+)[\"']"
cli_re="cli[[:space:]]*=[[:space:]]*[\"']([^\"']+)[\"']"
examples=(examples/**/*.lms)

for example in "${examples[@]}"; do
  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ "$line" =~ $install_re ]]; then
      spec="${BASH_REMATCH[1]#github:}"
      if [[ "$spec" != *@* ]]; then
        echo "install-hosts: unsupported GitHub install (missing @TAG) in $example: $spec" >&2
        exit 1
      fi
      full_repo="${spec%@*}"
      tag="${spec##*@}"
      if [[ ! "$full_repo" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ || ! "$tag" =~ ^[A-Za-z0-9_.-]+$ ]]; then
        echo "install-hosts: invalid GitHub install in $example: $spec" >&2
        exit 1
      fi
      repo="${full_repo##*/}"
      case " ${tags_by_repo[$full_repo]-} " in
        *" $tag "*) ;;
        *) tags_by_repo["$full_repo"]="${tags_by_repo[$full_repo]-}${tags_by_repo[$full_repo]:+ }$tag" ;;
      esac
      owner_by_repo["$full_repo"]="$repo"
    fi
    if [[ "$line" =~ $cli_re ]]; then
      cli="${BASH_REMATCH[1]}"
      cli="${cli%%[[:space:]]*}"
      [[ -z "$cli" ]] || cli_names["$cli"]=1
    fi
  done < "$example"
done

for full_repo in "${!tags_by_repo[@]}"; do
  repo="${owner_by_repo[$full_repo]}"
  if [[ -n "${cli_names[$repo]+set}" ]]; then cli_repo["$repo"]="$full_repo"; fi

  if ! gh repo view "$full_repo" --json name >/dev/null 2>&1; then
    echo "install-hosts: cannot verify public repository $full_repo" >&2
    exit 1
  fi

  if ! release_tags="$(gh release list -R "$full_repo" -L 1000 --json tagName --jq '.[].tagName')"; then
    echo "install-hosts: release lookup failed for $full_repo" >&2
    exit 1
  fi
  # `gh release list` includes prereleases and is used only to validate exact
  # pins. Resolve `@latest` through GitHub's dedicated endpoint, which excludes
  # prereleases and drafts by contract. A staging prerelease must never replace
  # the host selected for an @latest public example.
  latest_tag=""
  if latest_tag="$(gh api "repos/$full_repo/releases/latest" --jq '.tag_name' 2>/dev/null)" && [[ -n "$latest_tag" ]]; then
    resolved_latest["$full_repo"]="$latest_tag"
  else
    latest_tag=""
  fi

  available_tags=()
  for requested_tag in ${tags_by_repo[$full_repo]}; do
    resolved_tag="$requested_tag"
    [[ "$requested_tag" == latest ]] && resolved_tag="$latest_tag"
    found=false
    # A release-qualification worker has already verified this exact staged
    # lmvideo candidate; its stable tag is not public until promotion.
    if [[ "$full_repo" == lmctl-ai/lmvideo && -n "${LMVIDEO_QUALIFY_TAG:-}" && "$requested_tag" == "$LMVIDEO_QUALIFY_TAG" ]]; then
      found=true
    fi
    if [[ -n "$resolved_tag" ]]; then
      while IFS= read -r listed_tag; do
        [[ "$listed_tag" == "$resolved_tag" ]] && found=true
      done <<< "$release_tags"
    fi
    if [[ "$found" != true ]]; then
      reason="$full_repo@$requested_tag has no published release; dependent examples are not checkable"
      echo "install-hosts: SKIP $full_repo@$requested_tag: $reason"
      unavailable_reason["$full_repo@$requested_tag"]="$reason"
    else
      available_tags+=("$resolved_tag")
    fi
  done

  if [[ "$full_repo" == lmctl-ai/lmvideo && -n "${LMVIDEO_QUALIFY_TAG:-}" ]]; then
    echo "install-hosts: deferring $full_repo to the verified candidate step ($LMVIDEO_QUALIFY_TAG)"
    continue
  fi

  if [[ ${#available_tags[@]} -eq 0 ]]; then
    echo "install-hosts: no released binary is available for $full_repo"
    continue
  fi
  tag="${available_tags[-1]}"

  tmp="$(mktemp -d "$runner_scratch/lmctl-host-download.XXXXXX")"
  if ! gh release download "$tag" -R "$full_repo" --pattern "$repo-$plat$ext" --pattern SHA256SUMS --dir "$tmp"; then
    rm -rf "$tmp"
    echo "install-hosts: release exists but $repo-$plat$ext or SHA256SUMS could not be downloaded from $full_repo@$tag" >&2
    exit 1
  fi
  asset="$tmp/$repo-$plat$ext"
  if [[ ! -s "$asset" || ! -s "$tmp/SHA256SUMS" ]]; then
    rm -rf "$tmp"
    echo "install-hosts: release exists but has no non-empty $repo-$plat$ext or SHA256SUMS asset: $full_repo@$tag" >&2
    exit 1
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    actual_sha="$(sha256sum < "$asset" | awk '{print $1}' | tr -d '\r\\')"
  else
    actual_sha="$(shasum -a 256 < "$asset" | awk '{print $1}' | tr -d '\r\\')"
  fi
  expected_sha="$(tr -d '\r' < "$tmp/SHA256SUMS" | awk -v name="$repo-$plat$ext" '$2 == name || $2 == "*" name {print $1; exit}')"
  if [[ -z "$expected_sha" || "$actual_sha" != "$expected_sha" ]]; then
    rm -rf "$tmp"
    echo "install-hosts: SHA256SUMS verification failed for $full_repo@$tag:$repo-$plat$ext" >&2
    exit 1
  fi
  pin_key="$full_repo@$tag:$repo-$plat$ext"
  if [[ "$full_repo" != lmctl-ai/lmvideo ]]; then
    pinned_sha="${PINNED_HOST_SHA256[$pin_key]-}"
    if [[ -z "$pinned_sha" || "$actual_sha" != "$pinned_sha" ]]; then
      rm -rf "$tmp"
      echo "install-hosts: pinned SHA-256 verification failed for $pin_key (got $actual_sha)" >&2
      exit 1
    fi
  fi
  cp "$asset" "$host_bin/$repo$ext"
  chmod +x "$host_bin/$repo$ext"
  rm -rf "$tmp"
  echo "install-hosts: installed $repo from $full_repo@$tag (sha256=$actual_sha) for declared pins: ${tags_by_repo[$full_repo]}"
done

for cli in "${!cli_names[@]}"; do
  if [[ -z "${cli_repo[$cli]+set}" ]]; then
    echo "install-hosts: cli=\"$cli\" has no matching GitHub install declaration" >&2
    exit 1
  fi
done

for example in "${examples[@]}"; do
  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ "$line" =~ $install_re ]]; then
      spec="${BASH_REMATCH[1]#github:}"
      full_repo="${spec%@*}"
      requested_tag="${spec##*@}"
      if [[ "$requested_tag" == latest ]]; then
        requested_tag="${resolved_latest[$full_repo]-latest}"
      fi
      ref_key="$full_repo@$requested_tag"
      if [[ -n "${unavailable_reason[$ref_key]+set}" ]]; then
        printf '%s\t%s\n' "$example" "${unavailable_reason[$ref_key]}" >> "$skip_file"
        break
      fi
    fi
  done < "$example"
done

if [[ -n "${GITHUB_ENV:-}" ]]; then
  printf 'LMCTL_UNAVAILABLE_EXAMPLES_FILE=%s\n' "$skip_file" >> "$GITHUB_ENV"
fi
export PATH="$host_bin:$PATH"
if [[ -n "${GITHUB_PATH:-}" ]]; then
  if command -v cygpath >/dev/null 2>&1; then cygpath -w "$host_bin" >> "$GITHUB_PATH"; else printf '%s\n' "$host_bin" >> "$GITHUB_PATH"; fi
fi
echo "install-hosts: host directory prepended to PATH: $host_bin"
