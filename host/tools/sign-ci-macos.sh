#!/usr/bin/env bash
#
# Developer ID sign the macOS host tools DMG that CI built for a release tag.
#
# `.github/workflows/release.yml` builds the macOS package on a tag push, but
# only ad-hoc signed: the Developer ID key is deliberately not given to CI
# (docs/decisions/0024-release-macos-package-built-on-ci-signed-locally.md). This script
# brings the CI build to the key instead. It
#
#   1. finds the release.yml run for the tag (by default the newest successful
#      tag-push run) and checks the tag still points at the commit it built;
#   2. downloads the DMG artifact from that run;
#   3. checks the artifact's build provenance attestation, so what gets signed
#      is provably what this repository's workflow built from that commit;
#   4. copies the image out, re-signs every bundle with
#      cmake/MacosCodesignBundle.cmake -- the code that signs a local build --
#      and makes a new image;
#   5. verifies it with verify-macos-dmg.sh;
#   6. uploads it to the tag's draft release.
#
# The usual entry point is the CMake target, which passes the identity and
# entitlements the build is configured with:
#
#   cmake --build <build-dir> --target sign-latest
#
# Usage: host/tools/sign-ci-macos.sh [vX.Y[.Z]] [options]
#
# See docs/release/release-procedure.md for where this fits in a release.

set -euo pipefail

PROGRAM=${0##*/}

WORKFLOW="release.yml"
ARTIFACT="host-tools-macos-arm64"
TAG=""
RUN_ID=""
OUT_DIR=""
UPLOAD=1
ATTESTATION=1

die() { printf '%s: error: %s\n' "$PROGRAM" "$*" >&2; exit 1; }
note() { printf '==> %s\n' "$*"; }
warn() { printf '%s: warning: %s\n' "$PROGRAM" "$*" >&2; }

usage() {
  cat <<EOF
Usage: $PROGRAM [vX.Y[.Z]] [options]

Downloads the macOS DMG that $WORKFLOW built for a release tag, re-signs it
with the Developer ID identity in this Mac's keychain, verifies it, and
uploads it to the tag's draft release. With no tag, takes the newest
successful tag-push run of $WORKFLOW.

Options:
  --run ID            Sign the artifact of this workflow run instead. A run
                      not started by a tag push is signed but never uploaded.
  --out DIR           Where to write the signed DMG (default: a new directory
                      under \$TMPDIR, printed at the end).
  --no-upload         Sign and verify, but do not upload.
  --no-attestation    Skip the provenance check. Only for runs from before
                      $WORKFLOW attested its packages; such a DMG cannot be
                      shown to be what the workflow built.
  -h, --help          Show this message.

Environment:
  MACOS_CODE_SIGN_IDENTITY      Signing identity. Defaults to the value in
                                CMakeLists.txt.
  MACOS_CODE_SIGN_ENTITLEMENTS  Optional entitlements plist for the bundles.
  CMAKE                         cmake executable (default: cmake on PATH).
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --run) [ $# -ge 2 ] || die "--run needs a value"; RUN_ID=$2; shift 2 ;;
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

TAG_PATTERN='^v[0-9]+\.[0-9]+(\.[0-9]+)?$'

# ---------------------------------------------------------------- preflight

[ "$(uname -s)" = "Darwin" ] || die "signing needs codesign and hdiutil, so this must run on macOS"

CMAKE=${CMAKE:-cmake}
command -v "$CMAKE" >/dev/null || die "cmake not found"
command -v gh >/dev/null || die "gh (the GitHub CLI) not found"
command -v codesign >/dev/null || die "codesign not found (install the Xcode command line tools)"
gh auth status >/dev/null 2>&1 || die "gh is not logged in; run 'gh auth login'"

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SIGN_MODULE="$REPO_ROOT/cmake/MacosCodesignBundle.cmake"
VERIFY="$REPO_ROOT/host/tools/verify-macos-dmg.sh"

IDENTITY=${MACOS_CODE_SIGN_IDENTITY:-}
if [ -z "$IDENTITY" ]; then
  # Keep the default in one place: read the one CMake would use.
  IDENTITY=$(sed -n '/^  MACOS_CODE_SIGN_IDENTITY$/{n;s/^  "\(.*\)"$/\1/p;}' "$REPO_ROOT/CMakeLists.txt" | head -1)
fi
[ -n "$IDENTITY" ] || die "could not determine the signing identity; set MACOS_CODE_SIGN_IDENTITY"
[ "$IDENTITY" != "-" ] || die "the identity is ad-hoc ('-'); configure the build with the Developer ID identity"
if ! security find-identity -v -p codesigning 2>/dev/null | grep -qF "$IDENTITY"; then
  die "signing identity not found in the keychain: $IDENTITY
    'security find-identity -v -p codesigning' lists what is available."
fi
note "signing identity: $IDENTITY"

REPO=$(cd "$REPO_ROOT" && gh repo view --json nameWithOwner --jq .nameWithOwner) \
  || die "could not determine the GitHub repository for $REPO_ROOT"

# ---------------------------------------------------------------- the run

RUN_FIELDS="databaseId,event,headBranch,headSha,conclusion,createdAt"

if [ -n "$RUN_ID" ]; then
  RUN_JSON=$(gh run view "$RUN_ID" -R "$REPO" --json "$RUN_FIELDS,workflowName") \
    || die "no workflow run $RUN_ID in $REPO"
else
  if [ -n "$TAG" ]; then
    printf '%s\n' "$TAG" | grep -Eq "$TAG_PATTERN" || die "tag must look like v3.1 or v3.1.2, not '$TAG'"
  fi
  # headBranch is the tag name for a tag push. The newest matching run wins;
  # a re-run of an older tag therefore counts as newer.
  RUN_JSON=$(gh run list -R "$REPO" --workflow "$WORKFLOW" --event push --status success \
      --limit 100 --json "$RUN_FIELDS" \
    | python3 -c '
import json, re, sys
tag, pattern = sys.argv[1], sys.argv[2]
for run in json.load(sys.stdin):
    name = run["headBranch"]
    if re.match(pattern, name) and (not tag or name == tag):
        print(json.dumps(run)); break
' "$TAG" "$TAG_PATTERN")
  [ -n "$RUN_JSON" ] || die "no successful tag-push run of $WORKFLOW${TAG:+ for $TAG} in $REPO"
fi

field() { printf '%s' "$RUN_JSON" | python3 -c 'import json,sys; print(json.load(sys.stdin)[sys.argv[1]])' "$1"; }
RUN_ID=$(field databaseId)
RUN_EVENT=$(field event)
RUN_REF=$(field headBranch)
RUN_SHA=$(field headSha)
RUN_CONCLUSION=$(field conclusion)

note "run $RUN_ID: $RUN_EVENT of $RUN_REF at ${RUN_SHA:0:8}, $(field createdAt)"
[ "$RUN_CONCLUSION" = "success" ] || die "run $RUN_ID concluded '$RUN_CONCLUSION', not success"

IS_TAG_RUN=0
if [ "$RUN_EVENT" = "push" ] && printf '%s\n' "$RUN_REF" | grep -Eq "$TAG_PATTERN"; then
  IS_TAG_RUN=1
  TAG=$RUN_REF
  # A tag moved after the run would have the release carry a package built
  # from a different commit than its tag names.
  TAG_SHA=$(gh api "repos/$REPO/commits/$TAG" --jq .sha) || die "tag $TAG is not on $REPO"
  [ "$TAG_SHA" = "$RUN_SHA" ] \
    || die "tag $TAG now points at ${TAG_SHA:0:8}, but run $RUN_ID built ${RUN_SHA:0:8}"
elif [ "$UPLOAD" -eq 1 ]; then
  note "run $RUN_ID was not started by a release tag; the signed DMG will not be uploaded"
  UPLOAD=0
fi

# ---------------------------------------------------------------- download

WORK=$(mktemp -d -t ultralight-sign)
MOUNT_POINT="$WORK/mount"
cleanup() {
  hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true
  rm -rf "$WORK"
}
trap cleanup EXIT

note "downloading $ARTIFACT from run $RUN_ID"
gh run download "$RUN_ID" -R "$REPO" -n "$ARTIFACT" -D "$WORK/ci" \
  || die "could not download $ARTIFACT from run $RUN_ID (artifacts expire; see the run's retention)"

shopt -s nullglob
DMGS=("$WORK"/ci/*.dmg)
shopt -u nullglob
[ ${#DMGS[@]} -eq 1 ] || die "expected one DMG in $ARTIFACT, found ${#DMGS[@]}"
CI_DMG=${DMGS[0]}
DMG_NAME=${CI_DMG##*/}

# ---------------------------------------------------------------- provenance

if [ "$ATTESTATION" -eq 1 ]; then
  note "checking build provenance of $DMG_NAME"
  ATTEST_ARGS=(--repo "$REPO"
               --signer-workflow "$REPO/.github/workflows/$WORKFLOW"
               --source-digest "$RUN_SHA"
               --deny-self-hosted-runners)
  if [ "$IS_TAG_RUN" -eq 1 ]; then
    ATTEST_ARGS+=(--source-ref "refs/tags/$TAG")
  fi
  gh attestation verify "$CI_DMG" "${ATTEST_ARGS[@]}" >/dev/null \
    || die "$DMG_NAME has no valid provenance attestation from $WORKFLOW at ${RUN_SHA:0:8}.
    Runs from before $WORKFLOW attested its packages have none; --no-attestation
    signs one anyway, without that assurance."
else
  warn "--no-attestation: not checking that $DMG_NAME is what $WORKFLOW built"
fi

# ---------------------------------------------------------------- re-sign

mkdir -p "$MOUNT_POINT" "$WORK/stage"
hdiutil attach "$CI_DMG" -nobrowse -readonly -mountpoint "$MOUNT_POINT" >/dev/null 2>&1 \
  || die "could not mount $DMG_NAME"
VOLUME_NAME=$(diskutil info -plist "$MOUNT_POINT" | plutil -extract VolumeName raw -o - -)
# Copy the volume as a whole: ditto keeps a symlink *inside* its source a
# symlink, but follows one named as the source, so copying entries one by one
# would copy the real /Applications in place of the Applications link. Then
# drop the volume's own housekeeping.
ditto "$MOUNT_POINT" "$WORK/stage"
hdiutil detach "$MOUNT_POINT" -quiet
rm -rf "$WORK/stage/.fseventsd" "$WORK/stage/.Trashes" "$WORK/stage/.Spotlight-V100" \
       "$WORK/stage/.DocumentRevisions-V100" "$WORK/stage/.TemporaryItems"
[ -L "$WORK/stage/Applications" ] || [ ! -e "$WORK/stage/Applications" ] \
  || die "the image's Applications entry was not copied as a symlink"

# Extended attributes such as Finder info make `codesign --strict` refuse a
# bundle; none belong in a release image.
xattr -cr "$WORK/stage"

APPS=$(find "$WORK/stage" -maxdepth 3 -name '*.app' -prune -print | sort)
[ -n "$APPS" ] || die "no .app bundles found in $DMG_NAME"

while IFS= read -r app; do
  "$CMAKE" -DBUNDLE="$app" -DIDENTITY="$IDENTITY" -DCODESIGN="$(command -v codesign)" \
           -DENTITLEMENTS="${MACOS_CODE_SIGN_ENTITLEMENTS:-}" \
           -P "$SIGN_MODULE" \
    || die "signing ${app##*/} failed"
done <<EOF
$APPS
EOF

if [ -z "$OUT_DIR" ]; then
  OUT_DIR=$(mktemp -d -t ultralight-signed)
fi
mkdir -p "$OUT_DIR"
DMG="$OUT_DIR/$DMG_NAME"

# Same filesystem and format as CPack's DragNDrop generator: HFS+, UDZO.
note "making $DMG"
hdiutil create -volname "$VOLUME_NAME" -srcfolder "$WORK/stage" \
  -fs HFS+ -format UDZO -ov "$DMG" >/dev/null 2>&1 \
  || die "hdiutil could not create $DMG"

# ---------------------------------------------------------------- verify

"$VERIFY" "$DMG" "$IDENTITY" || exit 1

SHA=$(shasum -a 256 "$DMG" | awk '{print $1}')
SIZE=$(( $(stat -f%z "$DMG") / 1048576 ))

cat <<EOF

Signed and verified:
  $DMG
  $SIZE MB, sha256 $SHA
  built by run $RUN_ID from ${RUN_SHA:0:8}${TAG:+ ($TAG)}

EOF

# ---------------------------------------------------------------- upload

[ "$UPLOAD" -eq 1 ] || exit 0

if ! IS_DRAFT=$(gh release view "$TAG" -R "$REPO" --json isDraft --jq .isDraft 2>/dev/null); then
  die "there is no release for $TAG yet; the release job of run $RUN_ID opens it.
    Upload once it exists:
      gh release upload $TAG \"$DMG\""
fi
if [ "$IS_DRAFT" != "true" ]; then
  die "release $TAG is already published, so it is not changed here.
    To replace its macOS package deliberately:
      gh release upload $TAG \"$DMG\" --clobber"
fi

note "uploading to the draft release $TAG"
gh release upload "$TAG" "$DMG" -R "$REPO" --clobber
cat <<EOF

Uploaded. Check the draft and publish it:
  gh release view $TAG -R $REPO --web

EOF
