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

set -u -o pipefail

DURATION="${1:-120}"
OUTPUT="${2:-imutag-power-$(date +%Y%m%d-%H%M%S).csv}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="$HERE/power_experiment.py"
CONFIGS="$HERE/power-configs"

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

echo "IMUTag power sweep: ${DURATION}s per point -> $OUTPUT"
echo

echo "=== idle ==="
total=$((total + 1))
if ! python3 "$RUNNER" --idle --duration "$DURATION" \
     --label idle --output "$OUTPUT"; then
  echo "  idle FAILED" >&2
  failures=$((failures + 1))
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
  fi
  echo
done

echo "sweep complete: $((total - failures))/$total passed, results in $OUTPUT"
[ "$failures" -eq 0 ]
