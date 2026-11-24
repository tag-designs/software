#!/usr/bin/env bash
#
# Authenticode-sign the Windows host tools ZIP that CI built for a release tag,
# on this machine, with Azure Artifact Signing.
#
# The counterpart of sign-ci-macos.sh. `.github/workflows/release.yml` can sign
# the Windows package itself (its sign-windows job), but only once a CI
# identity holds the signing role; until then -- or whenever CI signing is off
# -- the release job attaches the unsigned ZIP and this script replaces it. It
#
#   1. finds the release.yml run for the tag (by default the newest successful
#      tag-push run) and checks the tag still points at the commit it built;
#   2. downloads the Windows ZIP artifact from that run;
#   3. checks the artifact's build provenance attestation;
#   4. unpacks it and signs every .exe and .dll that is not already signed
#      (Microsoft's runtime DLLs keep Microsoft's signature) with jsign, as the
#      signed-in Azure user, who must hold Artifact Signing Certificate Profile
#      Signer on the account;
#   5. checks every .exe and .dll is now signed and timestamped, and repacks
#      the ZIP with the original's names, order and timestamps;
#   6. uploads it to the tag's draft release, replacing the unsigned one.
#
# It does not validate the certificate chain -- that needs Windows' trust
# store -- so check one signed file on Windows the first time; see
# docs/release/windows-code-signing.md.
#
# Usage: host/tools/sign-ci-windows.sh [vX.Y[.Z]] [options]

set -euo pipefail

PROGRAM=${0##*/}

WORKFLOW="release.yml"
ARTIFACT="host-tools-windows-x64"
TAG=""
RUN_ID=""
ZIP_FILE=""
OUT_DIR=""
UPLOAD=1
ATTESTATION=1
TOKEN_PYTHON_DEFAULT="$HOME/.venvs/azure-signing/bin/python"

die() { printf '%s: error: %s\n' "$PROGRAM" "$*" >&2; exit 1; }
note() { printf '==> %s\n' "$*"; }
warn() { printf '%s: warning: %s\n' "$PROGRAM" "$*" >&2; }

usage() {
  cat <<EOF
Usage: $PROGRAM [vX.Y[.Z]] [options]

Downloads the Windows ZIP that $WORKFLOW built for a release tag, signs its
executables and DLLs with Azure Artifact Signing as the signed-in user, checks
them, and uploads the signed ZIP to the tag's draft release. With no tag,
takes the newest successful tag-push run of $WORKFLOW.

Options:
  --run ID            Sign the artifact of this workflow run instead. A run
                      not started by a tag push is signed but never uploaded.
  --zip FILE          Sign this local ZIP instead of a workflow artifact, for
                      testing; implies --no-upload and skips the provenance
                      check.
  --out DIR           Where to write the signed ZIP (default: a new directory
                      under \$TMPDIR, printed at the end).
  --no-upload         Sign and check, but do not upload.
  --no-attestation    Skip the provenance check. Only for runs from before
                      $WORKFLOW attested the Windows package.
  -h, --help          Show this message.

Environment (each defaults to the repository variable of the same name, read
with 'gh variable get', so CI and this script share one configuration):
  ARTIFACT_SIGNING_ENDPOINT   Account endpoint, e.g. https://eus.codesigning.azure.net/
  ARTIFACT_SIGNING_ACCOUNT    Artifact Signing account name
  ARTIFACT_SIGNING_PROFILE    Certificate profile name
  AZURE_TENANT_ID             Entra ID tenant of the account
Other:
  ARTIFACT_SIGNING_TOKEN      Use this access token instead of signing in.
  AZURE_SIGNING_PYTHON        Python with azure-identity, used to sign in when
                              the Azure CLI is not installed
                              (default: $TOKEN_PYTHON_DEFAULT).
  JSIGN                       jsign executable (default: jsign on PATH).
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --run) [ $# -ge 2 ] || die "--run needs a value"; RUN_ID=$2; shift 2 ;;
    --zip) [ $# -ge 2 ] || die "--zip needs a value"; ZIP_FILE=$2; shift 2 ;;
    --out) [ $# -ge 2 ] || die "--out needs a value"; OUT_DIR=$2; shift 2 ;;
    --no-upload) UPLOAD=0; shift ;;
    --no-attestation) ATTESTATION=0; shift ;;
    -*) die "unknown option: $1" ;;
    *)
      [ -z "$TAG" ] || die "unexpected extra argument: $1"
      TAG=$1; shift ;;
  esac
done

[ -z "$TAG" ] || [ -z "$RUN_ID" ] || die "give a tag or --run, not both"
[ -z "$ZIP_FILE" ] || { [ -z "$TAG" ] && [ -z "$RUN_ID" ]; } || die "--zip takes no tag or --run"
if [ -n "$ZIP_FILE" ]; then
  [ -f "$ZIP_FILE" ] || die "no such file: $ZIP_FILE"
  ZIP_FILE=$(cd "$(dirname "$ZIP_FILE")" && pwd)/${ZIP_FILE##*/}
  UPLOAD=0
  ATTESTATION=0
fi

TAG_PATTERN='^v[0-9]+\.[0-9]+(\.[0-9]+)?$'

# ---------------------------------------------------------------- preflight

JSIGN=${JSIGN:-jsign}
command -v "$JSIGN" >/dev/null || die "jsign not found; install it with 'brew install jsign'"
command -v gh >/dev/null || die "gh (the GitHub CLI) not found"
command -v openssl >/dev/null || die "openssl not found"
gh auth status >/dev/null 2>&1 || die "gh is not logged in; run 'gh auth login'"

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
PE_TOOL="$REPO_ROOT/host/tools/pe-signature.py"
TOKEN_TOOL="$REPO_ROOT/host/tools/azure-signing-token.py"

REPO=$(cd "$REPO_ROOT" && gh repo view --json nameWithOwner --jq .nameWithOwner) \
  || die "could not determine the GitHub repository for $REPO_ROOT"

# One configuration for CI and here: the repository variables, unless the
# environment overrides them.
config() {
  local name=$1 value=${!1:-}
  if [ -z "$value" ]; then
    value=$(gh variable get "$name" -R "$REPO" 2>/dev/null || true)
  fi
  [ -n "$value" ] || die "$name is not set, in the environment or as a repository variable of $REPO"
  printf '%s' "$value"
}
ENDPOINT=$(config ARTIFACT_SIGNING_ENDPOINT)
ACCOUNT=$(config ARTIFACT_SIGNING_ACCOUNT)
PROFILE=$(config ARTIFACT_SIGNING_PROFILE)
TENANT=$(config AZURE_TENANT_ID)
# jsign takes the endpoint as a bare host name.
ENDPOINT_HOST=${ENDPOINT#https://}
ENDPOINT_HOST=${ENDPOINT_HOST%%/*}
note "signing as $ACCOUNT/$PROFILE at $ENDPOINT_HOST (tenant $TENANT)"

WORK=$(mktemp -d -t ultralight-sign-windows)
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

if [ -n "$ZIP_FILE" ]; then
  note "signing the local file $ZIP_FILE (not uploaded)"
  CI_ZIP=$ZIP_FILE
  ZIP_NAME=${CI_ZIP##*/}
  RUN_ID="(none)"
  RUN_SHA="local file"
else

# ---------------------------------------------------------------- the run

RUN_FIELDS="databaseId,event,headBranch,headSha,status,conclusion,createdAt"

if [ -n "$RUN_ID" ]; then
  RUN_JSON=$(gh run view "$RUN_ID" -R "$REPO" --json "$RUN_FIELDS,workflowName") \
    || die "no workflow run $RUN_ID in $REPO"
else
  if [ -n "$TAG" ]; then
    printf '%s\n' "$TAG" | grep -Eq "$TAG_PATTERN" || die "tag must look like v3.1 or v3.1.2, not '$TAG'"
  fi
  RUN_JSON=$(gh run list -R "$REPO" --workflow "$WORKFLOW" --event push --status success \
      --limit 100 --json "$RUN_FIELDS" \
    | python3 -I -c '
import json, re, sys
tag, pattern = sys.argv[1], sys.argv[2]
for run in json.load(sys.stdin):
    name = run["headBranch"]
    if re.match(pattern, name) and (not tag or name == tag):
        print(json.dumps(run)); break
' "$TAG" "$TAG_PATTERN")
  [ -n "$RUN_JSON" ] || die "no successful tag-push run of $WORKFLOW${TAG:+ for $TAG} in $REPO"
fi

field() { printf '%s' "$RUN_JSON" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)[sys.argv[1]])' "$1"; }
RUN_ID=$(field databaseId)
RUN_EVENT=$(field event)
RUN_REF=$(field headBranch)
RUN_SHA=$(field headSha)
RUN_STATUS=$(field status)
RUN_CONCLUSION=$(field conclusion)

note "run $RUN_ID: $RUN_EVENT of $RUN_REF at ${RUN_SHA:0:8}, $(field createdAt)"
[ "$RUN_STATUS" = "completed" ] \
  || die "run $RUN_ID has not finished (status: $RUN_STATUS); wait for it with 'gh run watch $RUN_ID'"
[ "$RUN_CONCLUSION" = "success" ] || die "run $RUN_ID concluded '$RUN_CONCLUSION', not success"

IS_TAG_RUN=0
if [ "$RUN_EVENT" = "push" ] && printf '%s\n' "$RUN_REF" | grep -Eq "$TAG_PATTERN"; then
  IS_TAG_RUN=1
  TAG=$RUN_REF
  TAG_SHA=$(gh api "repos/$REPO/commits/$TAG" --jq .sha) || die "tag $TAG is not on $REPO"
  [ "$TAG_SHA" = "$RUN_SHA" ] \
    || die "tag $TAG now points at ${TAG_SHA:0:8}, but run $RUN_ID built ${RUN_SHA:0:8}"
elif [ "$UPLOAD" -eq 1 ]; then
  note "run $RUN_ID was not started by a release tag; the signed ZIP will not be uploaded"
  UPLOAD=0
fi

# ---------------------------------------------------------------- download

note "downloading $ARTIFACT from run $RUN_ID"
gh run download "$RUN_ID" -R "$REPO" -n "$ARTIFACT" -D "$WORK/ci" \
  || die "could not download $ARTIFACT from run $RUN_ID (artifacts expire; see the run's retention)"

shopt -s nullglob
ZIPS=("$WORK"/ci/*.zip)
shopt -u nullglob
[ ${#ZIPS[@]} -eq 1 ] || die "expected one ZIP in $ARTIFACT, found ${#ZIPS[@]}"
CI_ZIP=${ZIPS[0]}
ZIP_NAME=${CI_ZIP##*/}

# ---------------------------------------------------------------- provenance

if [ "$ATTESTATION" -eq 1 ]; then
  note "checking build provenance of $ZIP_NAME"
  ATTEST_ARGS=(--repo "$REPO"
               --signer-workflow "$REPO/.github/workflows/$WORKFLOW"
               --source-digest "$RUN_SHA"
               --deny-self-hosted-runners)
  if [ "$IS_TAG_RUN" -eq 1 ]; then
    ATTEST_ARGS+=(--source-ref "refs/tags/$TAG")
  fi
  gh attestation verify "$CI_ZIP" "${ATTEST_ARGS[@]}" >/dev/null \
    || die "$ZIP_NAME has no valid provenance attestation from $WORKFLOW at ${RUN_SHA:0:8}.
    Runs from before $WORKFLOW attested the Windows package have none;
    --no-attestation signs one anyway, without that assurance."
else
  warn "--no-attestation: not checking that $ZIP_NAME is what $WORKFLOW built"
fi

fi  # --zip

# ---------------------------------------------------------------- sign

mkdir -p "$WORK/stage"
ditto -x -k "$CI_ZIP" "$WORK/stage" || die "could not unpack $ZIP_NAME"

UNSIGNED=()
while IFS= read -r file; do
  UNSIGNED+=("$file")
done < <(python3 -I "$PE_TOOL" unsigned "$WORK/stage")
[ ${#UNSIGNED[@]} -gt 0 ] || die "$ZIP_NAME has no unsigned .exe or .dll; is it already signed?"
note "${#UNSIGNED[@]} files to sign"
for file in "${UNSIGNED[@]}"; do printf '    %s\n' "${file#"$WORK/stage/"}"; done

if [ -z "${ARTIFACT_SIGNING_TOKEN:-}" ]; then
  note "signing in to Azure (tenant $TENANT)"
  if command -v az >/dev/null; then
    ARTIFACT_SIGNING_TOKEN=$(az account get-access-token --tenant "$TENANT" \
        --resource https://codesigning.azure.net --query accessToken -o tsv) \
      || die "az could not get a token; run 'az login --tenant $TENANT'"
  else
    TOKEN_PYTHON=${AZURE_SIGNING_PYTHON:-$TOKEN_PYTHON_DEFAULT}
    [ -x "$TOKEN_PYTHON" ] || die "neither the Azure CLI nor $TOKEN_PYTHON is available to sign in.
    Either install the Azure CLI, or:
      python3 -m venv ~/.venvs/azure-signing
      ~/.venvs/azure-signing/bin/pip install azure-identity"
    ARTIFACT_SIGNING_TOKEN=$("$TOKEN_PYTHON" -I "$TOKEN_TOOL" "$TENANT") \
      || die "could not sign in to Azure"
  fi
fi
export ARTIFACT_SIGNING_TOKEN

# The token is passed through the environment, not the command line, so it
# does not show in the process list.
note "signing with jsign"
"$JSIGN" --storetype TRUSTEDSIGNING \
         --keystore "$ENDPOINT_HOST" \
         --storepass env:ARTIFACT_SIGNING_TOKEN \
         --alias "$ACCOUNT/$PROFILE" \
         --alg SHA-256 \
         --tsaurl http://timestamp.acs.microsoft.com --tsmode RFC3161 \
         --name "Ultralight Tags host tools" \
         --url "https://github.com/$REPO" \
         "${UNSIGNED[@]}" \
  || die "jsign failed; a 403 means this account lacks Certificate Profile Signer on $ACCOUNT"

# ---------------------------------------------------------------- check

note "checking every signature"
python3 -I "$PE_TOOL" check "$WORK/stage" \
  || die "some files are unsigned or have no timestamp; see above"

if [ -z "$OUT_DIR" ]; then
  OUT_DIR=$(mktemp -d -t ultralight-signed-windows)
fi
mkdir -p "$OUT_DIR"
ZIP="$OUT_DIR/$ZIP_NAME"
python3 -I "$PE_TOOL" repack "$CI_ZIP" "$WORK/stage" "$ZIP" || die "could not repack $ZIP"

SHA=$(shasum -a 256 "$ZIP" | awk '{print $1}')
SIZE=$(( $(stat -f%z "$ZIP") / 1048576 ))

cat <<EOF

Signed and checked:
  $ZIP
  $SIZE MB, sha256 $SHA
  $( [ -n "$ZIP_FILE" ] && echo "from $ZIP_FILE" || echo "built by run $RUN_ID from ${RUN_SHA:0:8}${TAG:+ ($TAG)}")

EOF

# ---------------------------------------------------------------- upload

[ "$UPLOAD" -eq 1 ] || exit 0

if ! IS_DRAFT=$(gh release view "$TAG" -R "$REPO" --json isDraft --jq .isDraft 2>/dev/null); then
  die "there is no release for $TAG yet; the release job of run $RUN_ID opens it.
    Upload once it exists:
      gh release upload $TAG \"$ZIP\" --clobber"
fi
if [ "$IS_DRAFT" != "true" ]; then
  die "release $TAG is already published, so it is not changed here.
    To replace its Windows package deliberately:
      gh release upload $TAG \"$ZIP\" --clobber"
fi

note "uploading to the draft release $TAG, replacing the unsigned ZIP"
gh release upload "$TAG" "$ZIP" -R "$REPO" --clobber
cat <<EOF

Uploaded. Check the draft and publish it:
  gh release view $TAG -R $REPO --web

EOF
