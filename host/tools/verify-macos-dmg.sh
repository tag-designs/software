#!/usr/bin/env bash
#
# Verify the signatures a user gets from a macOS host tools DMG.
#
# Mounts the image read-only and checks every .app bundle in it and every
# command line tool in its bin directory: each must pass `codesign --verify
# --strict` and its leaf authority must be the expected identity, and a tool
# may link only system libraries. Each bundle must also carry Qt's
# SecureTransport TLS plugin, which no signature check would miss. Verifying inside the mounted image, not a build tree, is
# the point: it is what a user receives, whoever built and signed it.
#
# Shared by both release paths: release-macos.sh (built and signed locally)
# and sign-ci-macos.sh (built by CI, re-signed locally).
#
# With --notarized it also requires what notarization adds: a valid ticket
# stapled to the image, Gatekeeper accepting the image and every bundle as
# notarized, and Apple holding a ticket for every tool.
#
# Usage: host/tools/verify-macos-dmg.sh DMG IDENTITY [--notarized]
#
# Exits 0 when every check passes, 1 when any does not, 2 on bad usage.

set -euo pipefail

PROGRAM=${0##*/}

die() { printf '%s: error: %s\n' "$PROGRAM" "$*" >&2; exit 1; }
note() { printf '==> %s\n' "$*"; }

usage() { printf 'Usage: %s DMG IDENTITY [--notarized]\n' "$PROGRAM" >&2; exit 2; }
[ $# -eq 2 ] || [ $# -eq 3 ] || usage
DMG=$1
IDENTITY=$2
NOTARIZED=0
if [ $# -eq 3 ]; then
  [ "$3" = "--notarized" ] || usage
  NOTARIZED=1
fi
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
  if [ "$NOTARIZED" -eq 1 ]; then
    # spctl reports the source it accepted the code under; only a ticket
    # makes that "Notarized Developer ID".
    source=$(spctl --assess --type exec -vv "$app" 2>&1 | sed -n 's/^source=//p' | head -1)
    if [ "$source" != "Notarized Developer ID" ]; then
      printf '  %-24s not accepted as notarized: %s\n' "$name" "${source:-rejected}"
      FAILED=1
      continue
    fi
  fi
  # Not a signature, but only the shipped image shows it: without a TLS
  # backend every HTTPS request fails (qtprogram's firmware release list).
  if [ ! -f "$app/Contents/PlugIns/tls/libqsecuretransportbackend.dylib" ]; then
    printf '  %-24s has no TLS plugin (PlugIns/tls)\n' "$name"
    FAILED=1
    continue
  fi
  printf '  %-24s ok\n' "$name"
done <<EOF
$APPS
EOF

# The command line tools are plain executables in tag_tools/bin. They carry no
# libraries, so anything they link must already be on every Mac. spctl
# rejects a bare executable as "not an app" however it is signed, so the
# notarization check asks codesign instead, which looks the ticket up online.
# A link, should one appear, must resolve to an executable in the image; its
# target is a bundle checked above.
TOOLS=$(find "$MOUNT_POINT" -maxdepth 3 -path '*/bin/*' \( -type f -o -type l \) -print | sort)
[ -n "$TOOLS" ] || die "no command line tools found in a bin directory of $DMG"

while IFS= read -r tool; do
  name=${tool##*/}
  if [ -L "$tool" ]; then
    if [ -x "$tool" ] && [ -f "$tool" ]; then
      printf '  %-24s ok (link)\n' "$name"
    else
      printf '  %-24s link does not resolve to an executable: %s\n' "$name" "$(readlink "$tool")"
      FAILED=1
    fi
    continue
  fi
  if ! codesign --verify --strict --verbose=1 "$tool" >/dev/null 2>&1; then
    printf '  %-24s FAILED codesign --verify\n' "$name"
    codesign --verify --strict --verbose=2 "$tool" 2>&1 | sed 's/^/      /'
    FAILED=1
    continue
  fi
  authority=$(codesign -dvv "$tool" 2>&1 | sed -n 's/^Authority=//p' | head -1)
  if [ "$authority" != "$IDENTITY" ]; then
    printf '  %-24s signed by an unexpected authority: %s\n' "$name" "${authority:-<none>}"
    FAILED=1
    continue
  fi
  foreign=$(otool -L "$tool" | tail -n +2 | awk '{print $1}' | grep -v -e '^/usr/lib/' -e '^/System/' || true)
  if [ -n "$foreign" ]; then
    printf '  %-24s links libraries the package does not carry:\n' "$name"
    printf '%s\n' "$foreign" | sed 's/^/      /'
    FAILED=1
    continue
  fi
  if [ "$NOTARIZED" -eq 1 ] \
     && ! codesign --verify --strict -R=notarized --check-notarization "$tool" >/dev/null 2>&1; then
    printf '  %-24s not notarized\n' "$name"
    FAILED=1
    continue
  fi
  printf '  %-24s ok\n' "$name"
done <<EOF
$TOOLS
EOF

if [ "$NOTARIZED" -eq 1 ]; then
  note "checking the notarization of the image"
  if xcrun stapler validate "$DMG" >/dev/null 2>&1; then
    printf '  %-24s ok\n' "stapled ticket"
  else
    printf '  %-24s missing or invalid\n' "stapled ticket"
    FAILED=1
  fi
  source=$(spctl --assess --type open --context context:primary-signature -vv "$DMG" 2>&1 \
           | sed -n 's/^source=//p' | head -1)
  if [ "$source" = "Notarized Developer ID" ]; then
    printf '  %-24s ok\n' "image assessment"
  else
    printf '  %-24s not accepted as notarized: %s\n' "image assessment" "${source:-rejected}"
    FAILED=1
  fi
fi

# Informational only. An image made on this Mac carries no quarantine
# attribute, so Gatekeeper accepts a valid Developer ID signature here even
# though the release is not notarized; a user who downloads an unnotarized
# image is asked to approve each app once. What the assessment does show is whether the
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
