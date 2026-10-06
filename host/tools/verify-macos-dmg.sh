#!/usr/bin/env bash
#
# Verify the signatures a user gets from a macOS host tools DMG.
#
# Mounts the image read-only and checks every .app bundle in it: the bundle
# must pass `codesign --verify --strict` and its leaf authority must be the
# expected identity. Verifying inside the mounted image, not a build tree, is
# the point: it is what a user receives, whoever built and signed it.
#
# Shared by both release paths: release-macos.sh (built and signed locally)
# and sign-ci-macos.sh (built by CI, re-signed locally).
#
# Usage: host/tools/verify-macos-dmg.sh DMG IDENTITY
#
# Exits 0 when every bundle verifies, 1 when any does not, 2 on bad usage.

set -euo pipefail

PROGRAM=${0##*/}

die() { printf '%s: error: %s\n' "$PROGRAM" "$*" >&2; exit 1; }
note() { printf '==> %s\n' "$*"; }

[ $# -eq 2 ] || { printf 'Usage: %s DMG IDENTITY\n' "$PROGRAM" >&2; exit 2; }
DMG=$1
IDENTITY=$2
[ -f "$DMG" ] || die "no such image: $DMG"

MOUNT_POINT=$(mktemp -d -t ultralight-dmg)
detach() {
  hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true
  rmdir "$MOUNT_POINT" 2>/dev/null || true
}
trap detach EXIT

note "verifying signatures in the mounted image"
hdiutil attach "$DMG" -nobrowse -readonly -mountpoint "$MOUNT_POINT" >/dev/null 2>&1 \
  || die "could not mount $DMG"

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

# Informational only. An image made on this Mac carries no quarantine
# attribute, so Gatekeeper accepts a valid Developer ID signature here even
# though the release is not notarized; a user who downloads it is asked to
# approve each app once. What the assessment does show is whether the
# signature is trusted at all.
FIRST_APP=$(printf '%s\n' "$APPS" | head -1)
printf '\nGatekeeper assessment of %s:\n' "${FIRST_APP##*/}"
spctl --assess --type exec -vv "$FIRST_APP" 2>&1 | sed 's/^/  /' || true

# Detach before reporting, so a failure message is not competing with a
# still-mounted image. `detach` tolerates a busy volume; a stuck mount must not
# turn a signature failure into a confusing early exit under `set -e`.
detach
trap - EXIT

[ "$FAILED" -eq 0 ] || die "signature verification failed; do not ship this image"
