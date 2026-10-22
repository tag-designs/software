#!/usr/bin/env bash
#
# Test a Linux host tools tarball in a clean Debian-family system, as a user
# would receive it.
#
# Meant to run as root inside a throwaway container; it installs packages.
# Normally started by `linux-build-check.sh --package`, which CI also uses.
# It installs only what a desktop distribution already has -- the display
# stack, OpenGL, fontconfig, D-Bus, CUPS -- plus the test's own tools, so a
# library the package should carry and does not shows up here as missing.
#
#   1. Self-containment: every ELF file in the package must resolve all its
#      libraries (ldd), which also catches a glibc older than the binaries
#      need, and carry no RUNPATH outside the package.
#   2. Command line tools: each answers --help; tag-info without a base exits
#      1; dataprocessing processes a fixture log; tag-txt-import converts a
#      legacy .txt fixture.
#   3. Qt applications, under Wayland (mutter, GNOME's compositor, headless)
#      and under X11 (Xvfb): each runs its documentation capture against the
#      repository's fixtures -- fake tags for qtmonitor, a recorded session
#      for qtcalibrate (its 3D orientation view included), logs for
#      sensorviz -- and must exit 0 with its screenshots written and not
#      blank, and nothing on stderr saying a plugin, library or QML module
#      failed to load. btviz and qtprogram have no capture mode; they must
#      start and stay up.
#
# Usage: linux-package-test.sh TARBALL SOURCE_DIR OUT_DIR
#   TARBALL     the package
#   SOURCE_DIR  a checkout, for host/docs/fixtures
#   OUT_DIR     receives the screenshots and logs
#
# Exits 0 when every check passes, 1 when any does not, 2 on bad usage.

set -uo pipefail

[ $# -eq 3 ] || { sed -n 's/^# \{0,1\}//; /^Usage:/,/^$/p' "$0" >&2; exit 2; }
tarball=$1 src=$2 out=$3
fixtures=$src/host/docs/fixtures
mkdir -p "$out"

failures=0
pass() { printf '  ok    %s\n' "$*"; }
fail() { printf '  FAIL  %s\n' "$*"; failures=$((failures + 1)); }
step() { printf '\n==> %s\n' "$*"; }

step "installing a desktop's libraries and the test's tools"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    libgl1 libegl1 libopengl0 libgl1-mesa-dri libglx-mesa0 libegl-mesa0 \
    libglib2.0-0t64 libfontconfig1 fonts-dejavu-core libdbus-1-3 libcups2 libudev1 \
    libxkbcommon0 libxkbcommon-x11-0 libx11-xcb1 libxcb-cursor0 \
    libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
    libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 \
    libwayland-client0 libwayland-cursor0 libwayland-egl1 \
    mutter dbus xvfb xauth imagemagick file binutils sqlite3 ca-certificates \
    >/dev/null || { echo "apt-get failed"; exit 1; }

step "unpacking $(basename "$tarball")"
root=/tmp/pkg
rm -rf "$root" && mkdir -p "$root"
tar -xzf "$tarball" -C "$root"
tt=$(find "$root" -maxdepth 2 -type d -name tag_tools | head -1)
[ -n "$tt" ] || { echo "no tag_tools directory in the package"; exit 1; }
echo "  $tt: $(du -sh "$tt" | cut -f1)"

# --- 1. Self-containment -------------------------------------------------------

step "self-containment"
elfs=$(find "$tt" -type f -exec file {} + | awk -F: '/ELF/ {print $1}')
missing=0
for f in $elfs; do
    if ldd "$f" 2>&1 | grep -q 'not found'; then
        fail "${f#$tt/}: $(ldd "$f" 2>&1 | grep 'not found' | sed 's/^[[:space:]]*//' | tr '\n' ' ')"
        missing=1
    fi
    runpath=$(readelf -d "$f" 2>/dev/null | sed -n 's/.*R\(UN\)\{0,1\}PATH.*\[\(.*\)\]/\2/p')
    for p in ${runpath//:/ }; do
        case $p in
            '$ORIGIN'*) ;;
            *) fail "${f#$tt/}: RUNPATH outside the package: $p"; missing=1 ;;
        esac
    done
done
[ $missing = 0 ] && pass "$(echo "$elfs" | wc -l) ELF files resolve every library inside the package"

# --- 2. Command line tools -------------------------------------------------------

step "command line tools"
for tool in tag-info tag-dwnld tag-test tag-reset tag-start tag-stop tag-cal tag-txt-import dataprocessing; do
    if [ ! -x "$tt/bin/$tool" ]; then
        fail "$tool is not in bin"
    elif "$tt/bin/$tool" --help >"$out/$tool-help.txt" 2>&1; then
        pass "$tool --help"
    else
        fail "$tool --help exited $?"
    fi
done
"$tt/bin/tag-info" >"$out/tag-info-no-base.txt" 2>&1
rc=$?
[ $rc = 1 ] && pass "tag-info with no base exits 1" || fail "tag-info with no base exited $rc"
if "$tt/bin/dataprocessing" -i "$fixtures/sensorviz/compasstag.db3" -o "$out/dataprocessing.db3" \
        -p compass-calibrated -p compass-orientation >"$out/dataprocessing.txt" 2>&1 \
   && [ "$(sqlite3 "$out/dataprocessing.db3" "SELECT count(*) FROM sqlite_master WHERE type='table'")" \
        -gt "$(sqlite3 "$fixtures/sensorviz/compasstag.db3" "SELECT count(*) FROM sqlite_master WHERE type='table'")" ]; then
    pass "dataprocessing derives streams from a CompassTag log"
else
    fail "dataprocessing on a CompassTag log; see dataprocessing.txt"
fi
txt_fixture=$src/host/commandline/test/txt-import/c1-fivemin.txt
if "$tt/bin/tag-txt-import" "$txt_fixture" -o "$out/txt-import.db3" >"$out/txt-import.txt" 2>&1 \
   && [ "$(sqlite3 "$out/txt-import.db3" "SELECT value FROM info WHERE fieldname = 'txt_roundtrip'")" = passed ]; then
    pass "tag-txt-import converts and round-trips a legacy BitTag .txt log"
else
    fail "tag-txt-import on a legacy .txt log; see txt-import.txt"
fi

# --- 3. Qt applications ----------------------------------------------------------

# Lines on stderr that mean the package is incomplete. Mesa's complaints about
# the container having no GPU, before it falls back to software rendering,
# are not among them.
broken='Could not load the Qt platform plugin|Could not find the Qt platform plugin|Cannot load library|is not installed|module ".*" plugin ".*" not found|Failed to load|QQmlApplicationEngine failed|cannot open shared object'

# run_capture PLATFORM NAME EXPECTED_PNGS -- COMMAND...
# Runs a documentation capture; it must exit 0, write at least EXPECTED_PNGS
# images that are not a single colour, and print nothing in $broken.
run_capture() {
    local platform=$1 name=$2 expect=$3; shift 4
    local dir=$out/$platform/$name
    mkdir -p "$dir"
    timeout 180 "$@" --screenshot-dir "$dir" >"$dir.log" 2>&1
    local rc=$?
    local pngs blank=0
    pngs=$(find "$dir" -name '*.png' | wc -l)
    for png in "$dir"/*.png; do
        [ -e "$png" ] || continue
        [ "$(identify -format '%k' "$png" 2>/dev/null)" -gt 1 ] || blank=$((blank + 1))
    done
    if [ $rc != 0 ]; then
        fail "$platform $name: exited $rc; see $platform/$name.log"
    elif [ "$pngs" -lt "$expect" ]; then
        fail "$platform $name: $pngs screenshots, expected $expect"
    elif [ $blank != 0 ]; then
        fail "$platform $name: $blank blank screenshots"
    elif grep -qE "$broken" "$dir.log"; then
        fail "$platform $name: $(grep -m1 -E "$broken" "$dir.log")"
    else
        pass "$platform $name: $pngs screenshots"
    fi
}

# run_stays_up PLATFORM NAME -- COMMAND...
# For an application with no capture mode: it must still be running after
# ten seconds, with nothing in $broken on stderr.
run_stays_up() {
    local platform=$1 name=$2; shift 3
    mkdir -p "$out/$platform"
    local log=$out/$platform/$name.log
    timeout 10 "$@" >"$log" 2>&1
    local rc=$?
    if [ $rc != 124 ]; then
        fail "$platform $name: exited $rc within ten seconds; see $platform/$name.log"
    elif grep -qE "$broken" "$log"; then
        fail "$platform $name: $(grep -m1 -E "$broken" "$log")"
    else
        pass "$platform $name: started and stayed up"
    fi
}

run_apps() {
    local p=$1 fixture name
    run_capture "$p" qtmonitor-startup 1 -- "$tt/bin/qtmonitor" --capture-startup-screenshot
    for fixture in "$fixtures"/qtmonitor/*.json; do
        name=$(basename "$fixture" .json)
        run_capture "$p" "qtmonitor-$name" 3 -- "$tt/bin/qtmonitor" \
            --fake-fixture "$fixture" --capture-main-screenshots
        run_capture "$p" "qtmonitor-$name-config" 1 -- "$tt/bin/qtmonitor" \
            --fake-fixture "$fixture" --capture-config-screenshots
    done
    local session
    session=$(ls "$fixtures"/qtcalibrate/*.json | head -1)
    run_capture "$p" qtcalibrate-replay 1 -- "$tt/bin/qtcalibrate" \
        --replay-capture "$session" --capture-replay-screenshots
    run_capture "$p" qtcalibrate-orientation 1 -- "$tt/bin/qtcalibrate" \
        --replay-capture "$session" --capture-orientation-screenshot
    for name in imutag compasstag; do
        run_capture "$p" "sensorviz-$name" 1 -- "$tt/bin/sensorviz" \
            --load-log "$fixtures/sensorviz/$name.db3" --capture-suite "$name" --no-user-prompts
    done
    run_stays_up "$p" btviz -- "$tt/bin/btviz"
    run_stays_up "$p" qtprogram -- "$tt/bin/qtprogram"
}

export XDG_RUNTIME_DIR=/tmp/xdg
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
export LANG=C.UTF-8

step "Qt applications under Wayland (mutter --headless)"
dbus-run-session -- mutter --headless --wayland --no-x11 --virtual-monitor 1280x900 \
    >"$out/mutter.log" 2>&1 &
mutter_pid=$!
for _ in $(seq 40); do [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && break; sleep 0.25; done
if [ -S "$XDG_RUNTIME_DIR/wayland-0" ]; then
    WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland run_apps wayland
else
    fail "mutter did not start; see mutter.log"
fi
kill $mutter_pid 2>/dev/null; wait $mutter_pid 2>/dev/null

step "Qt applications under X11 (Xvfb)"
Xvfb :99 -screen 0 1280x900x24 -nolisten tcp >"$out/xvfb.log" 2>&1 &
xvfb_pid=$!
sleep 1
DISPLAY=:99 QT_QPA_PLATFORM=xcb run_apps xcb
kill $xvfb_pid 2>/dev/null

step "result"
if [ $failures = 0 ]; then
    echo "  all checks passed"
    exit 0
fi
echo "  $failures checks failed"
exit 1
