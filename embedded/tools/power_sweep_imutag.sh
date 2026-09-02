#!/usr/bin/env bash
#
# Sweep IMUTag power across idle and every configured sample rate.
#
# This is the tag-level layer: it knows which configurations an IMUTag should be
# measured at and how long each needs. power_experiment.py knows nothing about
# sample rates, so a different tag gets a different script like this one rather
# than an edit to the runner.
#
# Each rate runs as an independent experiment: reset to idle with the clock set,
# start, measure detached, stop, download and sanity-check, erase. A failure in
# one rate does not stop the sweep; the CSV records it and the exit status
# reflects whether every rate passed.
#
# Usage:
#   power_sweep_imutag.sh [DURATION_SECONDS] [OUTPUT_CSV]
#
# Defaults to 120 s per rate, which is ample for a stable average at these
# currents and stays well inside the storage limit even at 1600 Hz (3.4 h).
# Raise it for tags whose average needs longer to settle.
#
# Examples:
#   power_sweep_imutag.sh                      # 120 s per rate
#   power_sweep_imutag.sh 600 results.csv      # 10 minutes per rate
#
# Idle is re-checked after every rate. Override the threshold or the check
# length with IDLE_LIMIT_UA and IDLE_CHECK_DURATION.
#
# Set STOP_ON_FAILURE=1 to halt on the first failing point and dump the state,
# stored configuration and marker log while they still exist. Use it when the
# failure is the subject rather than the power numbers:
#
#   STOP_ON_FAILURE=1 power_sweep_imutag.sh 60 results.csv
#
# Before running: the Joulescope desktop app must have released the device and
# qtmonitor must not be holding the monitor. The app blocks the driver outright;
# a held monitor keeps isMonitorEnabled() true so the tag never sleeps, which
# shows up only as a wrong average.

set -u -o pipefail

DURATION="${1:-120}"
OUTPUT="${2:-imutag-power-$(date +%Y%m%d-%H%M%S).csv}"

# An idle check between rate points is not optional bookkeeping. A tag that
# stops reaching Stop3 -- which boot-path and state-machine changes have done
# more than once -- invalidates every reading taken after it, and the rate
# measurements themselves cannot show it because they are dominated by the
# sensors. Checking idle between points bounds the damage to one interval.
IDLE_LIMIT_UA="${IDLE_LIMIT_UA:-50}"
IDLE_CHECK_DURATION="${IDLE_CHECK_DURATION:-20}"

# Halt on the first failing point and dump everything the tag can still tell
# us. Off by default so a normal sweep collects every point it can. Turn it on
# when the failure itself is the subject: the diagnosis depends on state that
# the next point's reset erases, so the sweep has to stop before touching the
# tag again.
STOP_ON_FAILURE="${STOP_ON_FAILURE:-0}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="$HERE/power_experiment.py"
CONFIGS="$HERE/power-configs"
BIN="${BIN:-$(cd "$HERE/../.." && pwd)/build-host/bin}"

if [ ! -x "$RUNNER" ] && [ ! -f "$RUNNER" ]; then
  echo "runner not found: $RUNNER" >&2
  exit 2
fi

# Idle is measured with no configuration at all; the rates each have one. The
# expected-hz value is passed only so the download check can compare the sample
# count against the measurement window.
RATES="100 200 400 800 1600"

failures=0
total=0
idle_faults=0

# Extract current_ua from the runner's "result: {json}" line.
current_from() {
  python3 -c '
import json, sys
value = None
for line in sys.stdin:
    if line.startswith("result: "):
        value = json.loads(line[len("result: "):]).get("current_ua")
if value is None:
    sys.exit(1)
print(f"{value:.4f}")
'
}

# Dump everything a failed tag can still report, without disturbing it.
#
# Order matters: nothing here may reset or erase. tag-reset erases from
# FINISHED and ABORTED, and the stored configuration and marker log are the
# whole evidence base for a start failure -- the configuration because a failed
# writeStoredConfig() is otherwise silent (it returns void and logs only to the
# compiled-out debug log), the marker log because CONFIGURED and RUNNING are
# both recorded before the abort is detectable, so the sequence is what
# distinguishes "never configured" from "configured then could not start".
dump_failure_state() {
  expected_hz="$1"
  echo
  echo "################ FAILURE at ${expected_hz} ################"
  echo "no reset or erase performed; state below is as the tag left it"
  echo
  echo "--- state, stored configuration and marker log ---"
  timeout 90 "$BIN/tag-info" 2>/dev/null | sed -n '/^tag_type/,$p'
  echo
  echo "--- decode ---"
  reported=$(timeout 90 "$BIN/tag-info" 2>/dev/null |
             sed -n 's/^  *odr: *S\([0-9]*\).*/\1/p' | tail -1)
  if [ -z "$reported" ]; then
    echo "  stored ODR: ABSENT from the reported configuration."
    echo "  A blank or unspecified ODR is what get_lsm_config() rejects, which"
    echo "  is how a failed stored-config write surfaces as an abort at start."
  elif [ "$reported" = "$expected_hz" ]; then
    echo "  stored ODR: S$reported, matches the requested ${expected_hz} Hz."
    echo "  The configuration write succeeded, so get_lsm_config() cannot be"
    echo "  the abort. Running(T_INIT) has exactly one other failure path:"
    echo "  initDataCollection(), which returns false only from"
    echo "  configure_mag_collection() (BMM350) or"
    echo "  bmp581_config_continuous_device() (BMP581). Both set ok = false and"
    echo "  then continue, so the sensors are left half configured and neither"
    echo "  reports which one failed outside the compiled-out debug log."
  else
    echo "  stored ODR: S$reported but ${expected_hz} Hz was requested."
    echo "  The stored configuration is wrong. Programming flash can only"
    echo "  clear bits, so a skipped erase yields the bitwise AND of the old"
    echo "  and new values."
  fi
}

# Confirm the tag still reaches standby. Returns nonzero when it does not, so
# the caller can mark every subsequent point as suspect.
check_idle_between() {
  label="$1"
  out=$(python3 "$RUNNER" --idle --duration "$IDLE_CHECK_DURATION" \
          --label "$label" --output "$OUTPUT" 2>&1)
  status=$?
  ua=$(printf '%s\n' "$out" | current_from)
  if [ $status -ne 0 ] || [ -z "$ua" ]; then
    echo "  IDLE CHECK FAILED to measure ($label)" >&2
    printf '%s\n' "$out" | tail -5 >&2
    return 1
  fi
  if awk -v v="$ua" -v lim="$IDLE_LIMIT_UA" 'BEGIN{exit !(v>lim)}'; then
    echo "  *** IDLE CHECK $ua uA EXCEEDS ${IDLE_LIMIT_UA} uA ***" >&2
    echo "  *** the tag is no longer reaching standby; later points are" >&2
    echo "  *** invalid until this is understood" >&2
    return 1
  fi
  echo "  idle check: $ua uA (limit ${IDLE_LIMIT_UA})"
  return 0
}

echo "IMUTag power sweep: ${DURATION}s per point -> $OUTPUT"
echo

echo "=== idle ==="
total=$((total + 1))
if ! python3 "$RUNNER" --idle --duration "$DURATION" \
     --label idle --output "$OUTPUT"; then
  echo "  idle FAILED" >&2
  failures=$((failures + 1))
  if [ "$STOP_ON_FAILURE" != "0" ]; then
    dump_failure_state idle
    exit 1
  fi
fi
echo

for hz in $RATES; do
  cfg="$CONFIGS/imutag-${hz}.json"
  echo "=== ${hz} Hz ==="
  if [ ! -f "$cfg" ]; then
    echo "  missing config: $cfg" >&2
    failures=$((failures + 1))
    total=$((total + 1))
    continue
  fi
  total=$((total + 1))
  if ! python3 "$RUNNER" --config "$cfg" --duration "$DURATION" \
       --label "${hz}Hz" --expected-hz "$hz" --output "$OUTPUT"; then
    echo "  ${hz} Hz FAILED" >&2
    failures=$((failures + 1))
    if [ "$STOP_ON_FAILURE" != "0" ]; then
      dump_failure_state "$hz"
      echo
      echo "sweep halted at ${hz} Hz; results so far in $OUTPUT" >&2
      exit 1
    fi
  fi
  if ! check_idle_between "idle-after-${hz}Hz"; then
    idle_faults=$((idle_faults + 1))
  fi
  echo
done

echo "sweep complete: $((total - failures))/$total passed, results in $OUTPUT"
if [ "$idle_faults" -ne 0 ]; then
  echo "WARNING: $idle_faults idle check(s) failed; readings after the first" >&2
  echo "         failure cannot be trusted" >&2
fi
[ "$failures" -eq 0 ] && [ "$idle_faults" -eq 0 ]
