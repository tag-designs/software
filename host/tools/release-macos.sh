#!/usr/bin/env bash
#
# Cut a host tools release and build the signed macOS package for it.
#
# The DMG takes its name from the highest `vX.Y[.Z]` tag reachable from HEAD,
# resolved at *configure* time. The tag therefore has to exist before CMake
# runs, which is why this script creates and pushes it first and only then
# configures. Pushing the tag also starts `.github/workflows/release.yml`,
# which builds the Windows package and opens a draft release.
#
# The Developer ID key stays on this machine: the macOS package is built and
# signed here, and uploading it to the draft release is a manual step.
#
# Usage: host/tools/release-macos.sh vX.Y[.Z] [options]
#
# See design/tag-release-procedure.md for where this fits in a release.

set -euo pipefail

PROGRAM=${0##*/}

PRESET="macos-vcpkg"
REMOTE="origin"
PUSH_TAG=1
KEEP_BUILD=0
TAG=""

die() { printf '%s: error: %s\n' "$PROGRAM" "$*" >&2; exit 1; }
note() { printf '==> %s\n' "$*"; }
warn() { printf '%s: warning: %s\n' "$PROGRAM" "$*" >&2; }

usage() {
  cat <<EOF
Usage: $PROGRAM vX.Y[.Z] [options]

Creates and pushes an annotated release tag, then configures, builds, signs
and verifies the macOS host tools DMG for that tag.

Options:
  --remote NAME       Git remote to push the tag to (default: $REMOTE).
  --no-push           Create the tag locally but do not push it. The package
                      still builds; nothing on GitHub is started.
  --preset NAME       CMake configure preset (default: $PRESET). The build
                      presets \`<preset>-release\` and \`<preset>-package\` are
                      used with it.
  --keep-build        Do not remove stale DMGs from the build tree first.
  -h, --help          Show this message.

Environment:
  MACOS_CODE_SIGN_IDENTITY  Overrides the signing identity that is checked for
                            and verified against. Defaults to the value CMake
                            uses.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --remote) [ $# -ge 2 ] || die "--remote needs a value"; REMOTE=$2; shift 2 ;;
    --preset) [ $# -ge 2 ] || die "--preset needs a value"; PRESET=$2; shift 2 ;;
    --no-push) PUSH_TAG=0; shift ;;
    --keep-build) KEEP_BUILD=1; shift ;;
    -*) die "unknown option: $1" ;;
    *)
      [ -z "$TAG" ] || die "unexpected extra argument: $1"
      TAG=$1; shift ;;
  esac
done

[ -n "$TAG" ] || { usage >&2; exit 2; }

BUILD_PRESET="${PRESET}-release"
PACKAGE_PRESET="${PRESET}-package"

# ---------------------------------------------------------------- preflight

[ "$(uname -s)" = "Darwin" ] || die "this script builds the macOS package and must run on macOS"

printf '%s\n' "$TAG" | grep -Eq '^v[0-9]+\.[0-9]+(\.[0-9]+)?$' \
  || die "tag must look like v3.1 or v3.1.2, not '$TAG'"

command -v git >/dev/null || die "git not found"
command -v cmake >/dev/null || die "cmake not found"
command -v codesign >/dev/null || die "codesign not found (install the Xcode command line tools)"

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$REPO_ROOT"
git rev-parse --git-dir >/dev/null 2>&1 || die "$REPO_ROOT is not a git repository"

IDENTITY=${MACOS_CODE_SIGN_IDENTITY:-}
if [ -z "$IDENTITY" ]; then
  # Keep the default in one place: read the one CMake would use.
  IDENTITY=$(sed -n '/^  MACOS_CODE_SIGN_IDENTITY$/{n;s/^  "\(.*\)"$/\1/p;}' CMakeLists.txt | head -1)
fi
[ -n "$IDENTITY" ] || die "could not determine the signing identity; set MACOS_CODE_SIGN_IDENTITY"

if ! security find-identity -v -p codesigning 2>/dev/null | grep -qF "$IDENTITY"; then
  die "signing identity not found in the keychain: $IDENTITY
    'security find-identity -v -p codesigning' lists what is available."
fi
note "signing identity: $IDENTITY"

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
  git status --short --untracked-files=no >&2
  die "working tree has uncommitted changes; a release must be reproducible from the commit"
fi
if [ -n "$(git status --porcelain --untracked-files=normal)" ]; then
  warn "untracked files present; they are not part of the release"
fi

HEAD_SHA=$(git rev-parse HEAD)

# ---------------------------------------------------------------- the tag

CREATED_TAG=0
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
  EXISTING=$(git rev-list -n 1 "$TAG")
  [ "$EXISTING" = "$HEAD_SHA" ] \
    || die "tag $TAG already exists and points at ${EXISTING:0:8}, not HEAD (${HEAD_SHA:0:8})"
  note "tag $TAG already exists at HEAD; reusing it"
else
  note "creating annotated tag $TAG at ${HEAD_SHA:0:8}"
  git tag -a "$TAG" -m "Host tools $TAG"
  CREATED_TAG=1
fi

if [ "$PUSH_TAG" -eq 1 ]; then
  REMOTE_TAG=$(git ls-remote --tags "$REMOTE" "refs/tags/$TAG" 2>/dev/null | awk 'NR==1{print $1}')
  if [ -z "$REMOTE_TAG" ]; then
    if ! git branch -r --contains "$HEAD_SHA" 2>/dev/null | grep -q .; then
      warn "HEAD is not on any remote-tracking branch; push the branch as well, or the release tag will point at a commit nobody else has"
    fi
    note "pushing $TAG to $REMOTE"
    git push "$REMOTE" "refs/tags/$TAG"
  else
    note "tag $TAG is already on $REMOTE; not pushing"
  fi
else
  note "--no-push: tag $TAG stays local"
fi

# ---------------------------------------------------------------- configure

CONFIG_LOG=$(mktemp -t ultralight-configure)
cleanup_log() { rm -f "$CONFIG_LOG"; }
trap cleanup_log EXIT

note "configuring with preset $PRESET"
cmake --preset "$PRESET" 2>&1 | tee "$CONFIG_LOG"

BUILD_DIR=$(sed -n 's/^-- Build files have been written to: //p' "$CONFIG_LOG" | tail -1)
[ -n "$BUILD_DIR" ] && [ -d "$BUILD_DIR" ] \
  || die "could not determine the build directory from the configure output"

CONFIGURED_TAG=$(sed -n 's/^-- UltralightTags package tag: //p' "$CONFIG_LOG" | tail -1)
if [ "$CONFIGURED_TAG" != "$TAG" ]; then
  die "CMake resolved the package tag as '$CONFIGURED_TAG', not '$TAG'.
    The lookup takes the *highest* vX.Y tag reachable from HEAD, not the newest
    one, so a higher version already merged here wins. Release from a commit
    where $TAG is the highest reachable tag, or pick a higher number."
fi

DMG="$BUILD_DIR/Ultralight-tags-$TAG.dmg"

if [ "$KEEP_BUILD" -eq 0 ]; then
  # A DMG left over from an earlier tag would otherwise be reported as this
  # one if packaging silently produced nothing.
  rm -f "$BUILD_DIR"/Ultralight-tags-*.dmg
fi

# ---------------------------------------------------------------- build

note "building"
cmake --build --preset "$BUILD_PRESET" --parallel

note "packaging"
cmake --build --preset "$PACKAGE_PRESET"

[ -f "$DMG" ] || die "expected $DMG, which packaging did not produce"

# ---------------------------------------------------------------- verify

# The apps are signed by the install rules in cmake/DeployQt.cmake, but CPack's
# DragNDrop generator gets CPACK_BUNDLE_APPLE_CERT_APP as well and can re-sign
# the bundle inside the image. What matters is the signature a user gets, so
# verify inside the mounted DMG rather than in the build tree.

MOUNT_POINT=$(mktemp -d -t ultralight-dmg)
detach() {
  hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true
  rmdir "$MOUNT_POINT" 2>/dev/null || true
  cleanup_log
}
trap detach EXIT

note "verifying signatures in the mounted image"
hdiutil attach "$DMG" -nobrowse -readonly -mountpoint "$MOUNT_POINT" >/dev/null

APPS=$(find "$MOUNT_POINT" -maxdepth 3 -name '*.app' -prune -print)
[ -n "$APPS" ] || die "no .app bundles found in $DMG"

FAILED=0
while IFS= read -r app; do
  name=${app##*/}
  if ! codesign --verify --strict --verbose=1 "$app" >/dev/null 2>&1; then
    printf '  %-24s FAILED codesign --verify\n' "$name"
    codesign --verify --strict --verbose=2 "$app" 2>&1 | sed 's/^/      /'
    FAILED=1
    continue
  fi
  authority=$(codesign -dvv "$app" 2>&1 | sed -n 's/^Authority=//p' | head -1)
  if [ "$authority" != "$IDENTITY" ]; then
    printf '  %-24s signed by an unexpected authority: %s\n' "$name" "${authority:-<none>}"
    FAILED=1
    continue
  fi
  printf '  %-24s ok\n' "$name"
done <<EOF
$APPS
EOF

# Gatekeeper will reject an unnotarized Developer ID app. That is expected --
# the release is deliberately not notarized -- but the assessment is worth
# seeing, because it distinguishes "unnotarized" from "broken signature".
FIRST_APP=$(printf '%s\n' "$APPS" | head -1)
printf '\nGatekeeper assessment of %s:\n' "${FIRST_APP##*/}"
spctl --assess --type exec -vv "$FIRST_APP" 2>&1 | sed 's/^/  /' || true

# Detach before reporting, so a failure message is not competing with a
# still-mounted image. `detach` tolerates a busy volume; a stuck mount must not
# turn a signature failure into a confusing early exit under `set -e`.
detach
trap cleanup_log EXIT

[ "$FAILED" -eq 0 ] || die "signature verification failed; do not ship this image"

# ---------------------------------------------------------------- report

SHA=$(shasum -a 256 "$DMG" | awk '{print $1}')
SIZE=$(( $(stat -f%z "$DMG") / 1048576 ))

cat <<EOF

Built and verified:
  $DMG
  $SIZE MB, sha256 $SHA

Upload it to the draft release when the Windows build has finished:
  gh release upload $TAG "$DMG"
  gh release view $TAG --web

EOF

if [ "$PUSH_TAG" -eq 0 ] && [ "$CREATED_TAG" -eq 1 ]; then
  printf 'The tag was not pushed, so no release exists yet:\n  git push %s refs/tags/%s\n\n' "$REMOTE" "$TAG"
fi
