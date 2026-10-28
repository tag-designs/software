#!/usr/bin/env bash
# screenshot.sh: captures a kit page, run against its mock engine, as a PNG.
#
#   screenshot.sh <page.html> <out.png> [query] [width height]
#
# Opens <page.html> from disk in headless Chrome with ?nopanel&theme=light
# and then <query> (the page's preview parameters, e.g. "scenario=finished"
# or "dialog=program&label=BitTag-017"), and writes <out.png> at
# width x height CSS pixels (default 900 x 697, the Qt screenshots' size) at
# scale 1. The page draws the browser-preview title bar in place of the
# window's own.
#
# Chrome is $CHROME if set, else Google Chrome's macOS path, else the first
# of google-chrome, chromium and chromium-browser on PATH. Nothing is
# installed; the page needs no server and no build.
set -euo pipefail

if [[ $# -lt 2 ]]; then
  sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//' >&2
  exit 2
fi
page=$1 out=$2 query=${3:-} width=${4:-900} height=${5:-697}

chrome=${CHROME:-}
if [[ -z $chrome ]]; then
  mac="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
  if [[ -x $mac ]]; then
    chrome=$mac
  else
    for c in google-chrome chromium chromium-browser; do
      if command -v "$c" >/dev/null; then chrome=$(command -v "$c"); break; fi
    done
  fi
fi
[[ -n $chrome ]] || { echo "screenshot.sh: no Chrome found; set CHROME" >&2; exit 1; }

page=$(cd "$(dirname "$page")" && pwd)/$(basename "$page")
mkdir -p "$(dirname "$out")"
out=$(cd "$(dirname "$out")" && pwd)/$(basename "$out")
# --virtual-time-budget lets the page's start-up promises, the mock's first
# state and any preview dialog run before the capture.
"$chrome" --headless --hide-scrollbars --force-device-scale-factor=1 \
  --virtual-time-budget=1500 --window-size="$width,$height" \
  --screenshot="$out" "file://$page?nopanel&theme=light${query:+&$query}" >/dev/null 2>&1
[[ -s $out ]] || { echo "screenshot.sh: $out was not written" >&2; exit 1; }
echo "$out"
