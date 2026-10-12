#!/bin/bash
#
# Provision a batch of tags from a released firmware image, one command.
#
# For each tag the operator attaches, in this order (the order matters; see
# AGENTS.md "The flashing protocol"):
#
#   1. read and record the board UUID        -- a board swap looks like a regression
#   2. check the supply the tag reports      -- the L432 maximum is 3.6 V
#   3. erase, if the tag is FINISHED/ABORTED -- the firmware that wrote the log erases it
#   4. flash, verified against its manifest
#   5. confirm the image that is now running
#   6. run the self-test
#   7. write one row to the batch CSV
#
# Every board gets its own log directory, and the CSV is the board-database
# row the release procedure asks for. Exit status is non-zero if any board
# failed, so this can gate a batch.
#
# Usage:  provision_tags.sh [-t TARGET] [-r RELEASE] [-n] [RELEASE_DIR]
#   -t TARGET   tag target to program          (default: PresTag)
#   -r RELEASE  release version to program     (default: fw-v0.6), resolved
#               under ~/Research/TagReleases/tag-firmware-<RELEASE>/
#   -n          non-interactive: do one tag and exit, do not prompt for more
#   RELEASE_DIR explicit directory, overriding -r: holds <Target>.elf and its
#               build manifest, or a release root containing firmware/<Target>/
#
# The release is selected by version, not by path, because several are kept
# side by side and flashing last month's by accident is easy and silent.
#
set -u -o pipefail

TARGET="PresTag"
RELEASE="${TAG_RELEASE:-fw-v0.6}"
INTERACTIVE=1
while getopts ":t:r:nh" opt; do
  case "$opt" in
    t) TARGET="$OPTARG" ;;
    r) RELEASE="$OPTARG" ;;
    n) INTERACTIVE=0 ;;
    h) sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "unknown option -$OPTARG; try -h" >&2; exit 2 ;;
  esac
done
shift $((OPTIND - 1))

# --- where things live -------------------------------------------------------
# Override any of these in the environment rather than editing the script.
RELEASES_ROOT="${TAG_RELEASES_ROOT:-$HOME/Research/TagReleases}"
RELEASE_DIR="${1:-${TAG_RELEASE_DIR:-$RELEASES_ROOT/tag-firmware-$RELEASE}}"
TOOLS="${TAG_TOOLS:-$HOME/Build/tag-designs/software/bin}"
REPO="${TAG_REPO:-$HOME/Research/tag-designs/software}"
FLASH="${TAG_FLASH:-$REPO/embedded/tools/flash_release.py}"
OUT_ROOT="${TAG_OUT:-$HOME/tags/provisioning}"

# Supply limits. PresTag and the other L432 tags run unregulated from a 2.5 V
# cell; the STM32L432 absolute maximum VDD is 3.6 V. A rig left at 3.7 V from
# an IMUTag session is both a meaningless measurement and out of spec.
VMIN="${TAG_VMIN:-2.30}"
VMAX="${TAG_VMAX:-2.65}"

die() { echo "ERROR: $*" >&2; exit 2; }

# --- resolve the tools, failing loudly and early -----------------------------
# tag-test, tag-info and tag-reset may not all live in one build directory.
find_tool() {
  local name="$1" c
  for c in "$TOOLS/$name" "$REPO/build-host/bin/$name" \
           "$HOME/Build/tag-designs/bin/$name" "$HOME/Build/clean/bin/$name"; do
    [ -x "$c" ] && { echo "$c"; return 0; }
  done
  return 1
}
TAG_INFO=$(find_tool tag-info)  || die "tag-info not found. Looked in $TOOLS, $REPO/build-host/bin, ~/Build/tag-designs/bin, ~/Build/clean/bin. Set TAG_TOOLS to a directory that has it."
TAG_RESET=$(find_tool tag-reset) || die "tag-reset not found (needed to erase a FINISHED/ABORTED tag before flashing). Set TAG_TOOLS."
TAG_TEST=$(find_tool tag-test)  || die "tag-test not found. Set TAG_TOOLS."
[ -x "$FLASH" ] || die "flash_release.py not found at $FLASH. Set TAG_FLASH or TAG_REPO."

# --- resolve the image -------------------------------------------------------
if   [ -f "$RELEASE_DIR/$TARGET.elf" ];                then IMAGE_DIR="$RELEASE_DIR"
elif [ -f "$RELEASE_DIR/firmware/$TARGET/$TARGET.elf" ]; then IMAGE_DIR="$RELEASE_DIR/firmware/$TARGET"
else
  echo "ERROR: no $TARGET.elf under $RELEASE_DIR" >&2
  echo "       tried $RELEASE_DIR/ and $RELEASE_DIR/firmware/$TARGET/" >&2
  if [ -d "$RELEASES_ROOT" ]; then
    echo "       releases available in $RELEASES_ROOT:" >&2
    ls -1 "$RELEASES_ROOT" 2>/dev/null | sed 's/^tag-firmware-/         -r /' >&2
    echo "       targets in this release:" >&2
    ls -1 "$RELEASE_DIR/firmware" 2>/dev/null | sed 's/^/         -t /' >&2
  fi
  exit 2
fi
MANIFEST=$(ls "$IMAGE_DIR"/*build-manifest.json 2>/dev/null | head -1)
[ -n "$MANIFEST" ] || die "no build manifest beside $IMAGE_DIR/$TARGET.elf -- flash_release.py verifies the image against it, so this is not a release directory."
REL_DESCRIBE=$(python3 -c "import json;print(json.load(open('$MANIFEST'))['source'].get('describe','?'))" 2>/dev/null || echo '?')
REL_COMMIT=$(python3   -c "import json;print(json.load(open('$MANIFEST'))['source'].get('commit_short','?'))" 2>/dev/null || echo '?')

SESSION="$OUT_ROOT/$TARGET-$(date -u +%Y%m%d-%H%M%S)"
mkdir -p "$SESSION" || die "cannot create $SESSION"
CSV="$SESSION/batch.csv"
echo "when_utc,uuid,target,release,commit,supply_v,erased,flashed,githash,selftest,verdict" > "$CSV"

echo "Provisioning $TARGET from $REL_DESCRIBE ($REL_COMMIT)"
echo "  image   : $IMAGE_DIR/$TARGET.elf"
echo "  tools   : $(dirname "$TAG_INFO")"
echo "  logs    : $SESSION"
echo "  supply  : must read ${VMIN}-${VMAX} V at the tag"
echo

# Monitor commands are issued twice: the first attach to a sleeping tag often
# fails on this rig ("initial DEMCR read failed"). Safe here because every
# command below runs against an idle or terminal tag, never a running one.
twice() { ( "$@" >/dev/null 2>&1 || true ); "$@" 2>&1; }

PASS=0; FAIL=0; NOTAG=0; N=0

provision_one() {
  N=$((N + 1))
  local log info uuid state volt githash erased=no flashed=no test=NO_RESULT verdict=FAIL

  info=$(twice "$TAG_INFO" 2>/dev/null | strings)
  uuid=$(echo "$info" | grep -oE '^uuid: "[0-9A-Fa-f]+"' | head -1 | grep -oE '[0-9A-Fa-f]{16,}')
  if [ -z "${uuid:-}" ]; then
    # Not a board failure: nothing was attached, or the attach timed out. Do
    # not count it against the batch, and do not write a row for a board that
    # was never identified.
    echo "  [$N] no tag responded -- is one attached and powered?"
    N=$((N - 1)); NOTAG=$((NOTAG + 1)); return 1
  fi
  log="$SESSION/$uuid"; mkdir -p "$log"
  echo "$info" > "$log/before.txt"
  state=$(echo "$info"  | grep -oE 'Current state: [A-Z_]+' | head -1 | awk '{print $3}')
  volt=$(echo  "$info"  | grep -oE 'vdd=[0-9.]+' | head -1 | cut -d= -f2)
  echo "  [$N] $uuid  state=${state:-?}  supply=${volt:-unknown}"

  # 2. supply. Refuse rather than program a tag that is out of spec.
  if [ -n "${volt:-}" ]; then
    if ! awk -v v="$volt" -v lo="$VMIN" -v hi="$VMAX" 'BEGIN{exit !(v>=lo && v<=hi)}'; then
      echo "      SUPPLY OUT OF RANGE: ${volt} V, expected ${VMIN}-${VMAX} V -- not programming."
      echo "      (the STM32L432 absolute maximum is 3.6 V; check the bench supply)"
      echo "$(date -u +%FT%TZ),$uuid,$TARGET,$REL_DESCRIBE,$REL_COMMIT,$volt,no,no,,,FAIL_SUPPLY" >> "$CSV"
      FAIL=$((FAIL + 1)); return 1
    fi
  else
    echo "      supply not reported by this image; continuing (it is checked again after the self-test)"
  fi

  # 3. erase first, and only from a state that can be erased.
  if [ "${state:-}" = "FINISHED" ] || [ "${state:-}" = "ABORTED" ]; then
    echo "      erasing (state $state) before flashing"
    twice "$TAG_RESET" --set-rtc > "$log/erase.log" 2>&1
    erased=yes
  fi

  # 4. flash, verified against the manifest.
  if "$FLASH" "$IMAGE_DIR" --label "$uuid" --json "$log/flash.json" > "$log/flash.log" 2>&1; then
    flashed=yes
  else
    echo "      FLASH FAILED -- see $log/flash.log"
    echo "$(date -u +%FT%TZ),$uuid,$TARGET,$REL_DESCRIBE,$REL_COMMIT,${volt:-},$erased,no,,,FAIL_FLASH" >> "$CSV"
    FAIL=$((FAIL + 1)); return 1
  fi

  # 5. confirm what is now running, and that it is still the same board.
  sleep 4
  local after
  after=$(twice "$TAG_INFO" 2>/dev/null | strings); echo "$after" > "$log/after.txt"
  githash=$(echo "$after" | grep -oE 'githash: "[0-9a-f]+"' | head -1 | grep -oE '[0-9a-f]{8}')
  if [ "$(echo "$after" | grep -oE '[0-9A-Fa-f]{24}' | head -1)" != "$uuid" ]; then
    echo "      BOARD CHANGED between flash and verify -- aborting this board"
    echo "$(date -u +%FT%TZ),$uuid,$TARGET,$REL_DESCRIBE,$REL_COMMIT,${volt:-},$erased,$flashed,${githash:-},,FAIL_IDENTITY" >> "$CSV"
    FAIL=$((FAIL + 1)); return 1
  fi

  # 6. self-test. Intermittent on some targets (host-side link timeouts), so
  #    one retry before calling it a failure.
  local t
  for t in 1 2; do
    test=$("$TAG_TEST" 2>&1 | tee "$log/selftest-$t.log" | strings \
           | grep -aoE 'ALL_PASSED|[A-Z0-9_]+FAILED' | tail -1)
    [ "${test:-}" = "ALL_PASSED" ] && break
    [ "$t" = 1 ] && echo "      self-test gave '${test:-no result}', retrying once"
  done
  : "${test:=NO_RESULT}"

  # supply again, now from the self-test, which reports it directly
  local v2
  v2=$(grep -ahoE '^Voltage: [0-9.]+' "$log/selftest-"*.log 2>/dev/null | tail -1 | awk '{print $2}')
  [ -n "${v2:-}" ] && volt="$v2"

  [ "$test" = "ALL_PASSED" ] && verdict=PASS
  echo "$(date -u +%FT%TZ),$uuid,$TARGET,$REL_DESCRIBE,$REL_COMMIT,${volt:-},$erased,$flashed,${githash:-},$test,$verdict" >> "$CSV"

  if [ "$verdict" = PASS ]; then
    echo "      PASS  githash=${githash:-?}  supply=${volt:-?} V  self-test=$test"
    PASS=$((PASS + 1))
  else
    echo "      FAIL  self-test=$test  (logs in $log)"
    FAIL=$((FAIL + 1))
  fi
}

if [ "$INTERACTIVE" = 0 ]; then
  provision_one
else
  echo "Attach a $TARGET and press Enter to program it. Press q then Enter when the batch is done."
  while true; do
    printf '\n> '
    IFS= read -r reply || break
    case "$reply" in q|Q|quit|exit) break ;; esac
    provision_one
  done
fi

echo
echo "================ batch summary ================"
echo "  target  : $TARGET from $REL_DESCRIBE ($REL_COMMIT)"
echo "  passed  : $PASS"
echo "  failed  : $FAIL"
[ "$NOTAG" -gt 0 ] && echo "  no tag  : $NOTAG (attach attempts with nothing responding)"
echo "  rows    : $CSV"
[ "$FAIL" -gt 0 ] && { echo; echo "Failed boards:"; awk -F, 'NR>1 && $NF!="PASS" {printf "    %s  %s\n", $2, $NF}' "$CSV"; }
exit $(( FAIL > 0 ? 1 : 0 ))
