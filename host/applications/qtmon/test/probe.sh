#!/usr/bin/env bash
# Run the configuration probe over the committed cases.
#
#   host/applications/qtmon/test/probe.sh <qtmonitor-executable> <out-dir>
#
# On macOS the executable is inside the bundle:
#   <build>/Release/bin/qtmonitor.app/Contents/MacOS/qtmonitor
#
# Runs offscreen and in UTC so the results do not depend on the display or the
# machine's time zone. See README.md in this directory.

set -eu

if [ $# -ne 2 ]; then
    echo "usage: $0 <qtmonitor-executable> <out-dir>" >&2
    exit 2
fi

here=$(cd "$(dirname "$0")" && pwd)
QT_QPA_PLATFORM=offscreen TZ=UTC \
    "$1" --config-probe "$here/cases" --config-probe-out "$2" \
    2> >(grep -v 'does not support propagateSizeHints' >&2)
