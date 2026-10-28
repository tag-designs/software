#!/usr/bin/env bash
# screenshots.sh: the user manual's Tag Programmer pictures, from this page
# and its mock engine (no app, no tag).
#
#   screenshots.sh [out-dir]        (default: host/docs/src/images)
#
# Each picture is a mock scenario or preview dialog (see app.js and
# mock-engine.js), captured by host/ui-kit/screenshot.sh. Rerun it after a
# change to the page or the mock, and check the pictures before committing.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/../../../docs/src/images}
shot() { "$here/../../../ui-kit/screenshot.sh" "$here/index.html" "$out/$1.png" "$2"; }

shot tagprogram-station        ""
shot tagprogram-program        "dialog=program&label=BitTag-017"
shot tagprogram-label-conflict "dialog=conflict&label=BitTag-017"
shot tagprogram-programming    "scenario=programming"
