#!/usr/bin/env bash
# screenshots.sh: the user manual's Tag Monitor pictures, from this page and
# its mock engine (no app, no tag).
#
#   screenshots.sh [out-dir]        (default: host/docs/src/images)
#
# Each picture is a mock scenario and tab (see app.js and mock-engine.js),
# captured by host/ui-kit/screenshot.sh at the window's opening size
# (app/tauri.conf.json). Rerun it after a change to the page or the mock,
# and check the pictures before committing.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/../../../docs/src/images}
shot() { "$here/../../../ui-kit/screenshot.sh" "$here/index.html" "$out/$1.png" "$2" 1100 640; }

shot tagmonitor-configuration "scenario=idle&page=config"
shot tagmonitor-tag-info      "scenario=finished&page=tag"
shot tagmonitor-choose-base   "scenario=prompt-base&page=tag"
