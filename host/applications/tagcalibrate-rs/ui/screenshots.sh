#!/usr/bin/env bash
# screenshots.sh: Tag Calibrator pictures, from this page and its mock
# engine (no app, no tag), for the user manual's Tag Calibrator page.
#
#   screenshots.sh [out-dir]        (default: host/docs/src/images)
#
# Each picture is a mock scenario and tab (see app.js and mock-engine.js),
# captured by host/ui-kit/screenshot.sh at 1040 x 800, the size qtcalibrate's
# pictures are taken at less its title bar. Rerun it after a change to the
# page or the mock, and check the pictures before committing.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/../../../docs/src/images}
shot() { "$here/../../../ui-kit/screenshot.sh" "$here/index.html" "$out/$1.png" "$2" 1040 800; }

shot tagcalibrate-startup        "scenario=startup"
shot tagcalibrate-collection-100 "scenario=collected&percent=100"
shot tagcalibrate-orientation    "scenario=orientation&page=orientation"
