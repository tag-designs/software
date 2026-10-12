#!/bin/bash
#
# Program and test a batch of tags from a released image: flash_release.py,
# then tag-test. If programming fails, say so and skip the test.
#
# Usage:  provision_tags.sh [-t TARGET] [-r RELEASE] [-n]
#   -t TARGET   target to program   (default: PresTag)
#   -r RELEASE  release version     (default: fw-v0.6), taken from
#               ~/Research/TagReleases/tag-firmware-<RELEASE>/firmware/<TARGET>
#   -n          program one tag and exit, without prompting for more
#
# Chosen by version rather than path because several releases sit side by side
# and flashing an old one would be silent.
#
set -u -o pipefail

TARGET="PresTag"; RELEASE="${TAG_RELEASE:-fw-v0.6}"; INTERACTIVE=1
while getopts ":t:r:nh" opt; do
  case "$opt" in
    t) TARGET="$OPTARG" ;; r) RELEASE="$OPTARG" ;; n) INTERACTIVE=0 ;;
    h) sed -n '2,15p' "$0"; exit 0 ;;
    *) echo "unknown option -$OPTARG; try -h" >&2; exit 2 ;;
  esac
done

RELEASES="${TAG_RELEASES_ROOT:-$HOME/Research/TagReleases}"
IMAGE_DIR="${TAG_IMAGE_DIR:-$RELEASES/tag-firmware-$RELEASE/firmware/$TARGET}"
REPO="${TAG_REPO:-$HOME/Research/tag-designs/software}"
FLASH="${TAG_FLASH:-$REPO/embedded/tools/flash_release.py}"
OUT="${TAG_OUT:-$HOME/tags/provisioning}/$TARGET-$(date -u +%Y%m%d-%H%M%S)"

die() { echo "ERROR: $*" >&2; exit 2; }

# tag-test may not live beside the other tools; take the first one found.
TAG_TEST=""
for c in "${TAG_TOOLS:-}/tag-test" "$HOME/Build/tag-designs/software/bin/tag-test" \
         "$REPO/build-host/bin/tag-test" "$HOME/Build/tag-designs/bin/tag-test"; do
  [ -x "$c" ] && { TAG_TEST="$c"; break; }
done
[ -n "$TAG_TEST" ] || die "tag-test not found; set TAG_TOOLS to the directory holding it"
[ -x "$FLASH" ]    || die "flash_release.py not found at $FLASH; set TAG_FLASH"
[ -f "$IMAGE_DIR/$TARGET.elf" ] || {
  echo "ERROR: no $TARGET.elf in $IMAGE_DIR" >&2
  [ -d "$RELEASES" ] && { echo "       releases:" >&2; ls -1 "$RELEASES" | sed 's/^tag-firmware-/         -r /' >&2; }
  exit 2
}

mkdir -p "$OUT" || die "cannot create $OUT"
CSV="$OUT/batch.csv"; echo "when_utc,uuid,flash,selftest,verdict" > "$CSV"
echo "Programming $TARGET from $RELEASE"
echo "  image : $IMAGE_DIR/$TARGET.elf"
echo "  test  : $TAG_TEST"
echo "  logs  : $OUT"

PASS=0; FAIL=0; N=0

one() {
  N=$((N + 1))
  local log="$OUT/board-$N" uuid test verdict
  mkdir -p "$log"

  if ! "$FLASH" "$IMAGE_DIR" --json "$log/flash.json" > "$log/flash.log" 2>&1; then
    echo "  [$N] PROGRAMMING FAILED -- skipping the test. See $log/flash.log"
    { grep -aiE 'error|failed|not found|cannot' "$log/flash.log" | head -3 \
        || tail -3 "$log/flash.log"; } | sed 's/^/        /'
    echo "$(date -u +%FT%TZ),,FAIL,skipped,FAIL" >> "$CSV"
    FAIL=$((FAIL + 1)); return 1
  fi

  sleep 3
  test=$(timeout 400 "$TAG_TEST" 2>&1 | tee "$log/selftest.log" | strings \
         | grep -aoE 'ALL_PASSED|[A-Z0-9_]+FAILED' | tail -1)
  uuid=$(strings "$log/selftest.log" | grep -aoE 'UUID: [0-9A-Fa-f]+' | head -1 | awk '{print $2}')
  : "${test:=NO_RESULT}"
  [ -n "${uuid:-}" ] && { mv "$log" "$OUT/$uuid" 2>/dev/null && log="$OUT/$uuid"; }

  if [ "$test" = ALL_PASSED ]; then verdict=PASS; PASS=$((PASS + 1)); else verdict=FAIL; FAIL=$((FAIL + 1)); fi
  echo "$(date -u +%FT%TZ),${uuid:-},OK,$test,$verdict" >> "$CSV"
  printf '  [%d] %s  programmed, self-test %s  -> %s\n' "$N" "${uuid:-unknown}" "$test" "$verdict"
}

if [ "$INTERACTIVE" = 0 ]; then
  one
else
  echo
  echo "Attach a $TARGET and press Enter. Press q then Enter when done."
  while true; do
    printf '\n> '; IFS= read -r reply || break
    case "$reply" in q|Q|quit|exit) break ;; esac
    one
  done
fi

echo
echo "passed $PASS, failed $FAIL   rows: $CSV"
[ "$FAIL" -gt 0 ] && awk -F, 'NR>1 && $NF!="PASS" {printf "  FAILED  %s  %s\n", ($2==""?"(unknown)":$2), $4}' "$CSV"
exit $(( FAIL > 0 ? 1 : 0 ))
