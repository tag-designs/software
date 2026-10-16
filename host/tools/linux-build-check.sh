#!/usr/bin/env bash
#
# Build the host tools on Linux, against the distribution's own packages, in a
# throwaway container.
#
# Follows the README's Linux instructions as a new user would: installs the
# README's apt packages into a stock distribution image, configures with the
# README's command, and builds. The point is to test a distribution Qt, which
# splits its tree across /usr/lib/qt6 and /usr/lib/<arch>/qt6 in ways an
# installer or aqt Qt never does.
#
# Leaves nothing behind but the base image (about 80 MB, removed by --rmi):
#   - the container is started with --rm;
#   - the checkout is mounted read-only, so a configure or build that writes
#     into the source tree fails here instead of littering it;
#   - the build tree is in the container's /tmp and goes with it.
# The cost is that every run downloads and installs the packages again.
#
# Tests the working tree as it stands, committed or not. ChibiOS is not
# needed: the build is host-only.
#
# With --package it builds nothing: it tests a Linux release tarball instead,
# running linux-package-test.sh in a fresh container of --image (default
# ubuntu:24.04 in this mode). That is the test CI runs on every package it
# builds; here it runs on any machine with Docker, x86_64 containers included
# on Apple silicon (OrbStack runs them under Rosetta). Screenshots and logs go
# to --out.
#
# Usage: host/tools/linux-build-check.sh [options]
#   --image IMAGE      Debian-family image to build in (default debian:13)
#   --target TARGET    Target to build (default qml_import_check_qtcalibrate);
#                      "all" builds everything the host build has
#   --without-quick3d  Leave out the Quick 3D packages; the QML import check
#                      should then fail and name the QtQuick3D modules
#   --rmi              Remove the base image afterwards
#   --shell            Open a shell in the container after the build, kept
#                      even when the build fails
#   --package TARBALL  Test this release tarball instead of building
#   --out DIR          Where --package writes screenshots and logs (default
#                      ./linux-package-test)
#
# Needs a running Docker (OrbStack or Docker Desktop). Host docs are not built
# (-DBUILD_HOST_DOCS=OFF): MkDocs is a pip install, not an apt package, and
# not what this checks.
#
# Exits with the build's or the package test's status, 2 on bad usage.

set -euo pipefail

PROGRAM=${0##*/}

die() { printf '%s: error: %s\n' "$PROGRAM" "$*" >&2; exit 1; }
usage() { sed -n 's/^# \{0,1\}//; /^Usage:/,/^$/p' "$0" >&2; exit 2; }

image=
target=qml_import_check_qtcalibrate
package=
out=linux-package-test
quick3d=1
rmi=0
shell=0
while [ $# -gt 0 ]; do
    case $1 in
        --image) [ $# -ge 2 ] || usage; image=$2; shift 2 ;;
        --target) [ $# -ge 2 ] || usage; target=$2; shift 2 ;;
        --without-quick3d) quick3d=0; shift ;;
        --rmi) rmi=1; shift ;;
        --shell) shell=1; shift ;;
        --package) [ $# -ge 2 ] || usage; package=$2; shift 2 ;;
        --out) [ $# -ge 2 ] || usage; out=$2; shift 2 ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

# OrbStack installs docker in ~/.orbstack/bin, which a non-login shell may not
# have on PATH.
docker=$(command -v docker || true)
[ -n "$docker" ] || [ ! -x "$HOME/.orbstack/bin/docker" ] || docker=$HOME/.orbstack/bin/docker
[ -n "$docker" ] || die "docker not found; install OrbStack or Docker Desktop"
"$docker" info >/dev/null 2>&1 || die "docker is installed but not running"

source_dir=$(cd "$(dirname "$0")/../.." && pwd)

if [ -n "$package" ]; then
    [ -f "$package" ] || die "no such package: $package"
    image=${image:-ubuntu:24.04}
    package_dir=$(cd "$(dirname "$package")" && pwd)
    package_name=$(basename "$package")
    mkdir -p "$out"
    out=$(cd "$out" && pwd)
    platform=()
    case $package_name in
        *x86_64*) platform=(--platform linux/amd64) ;;
        *aarch64*) platform=(--platform linux/arm64) ;;
    esac
    printf '==> testing %s in %s\n' "$package_name" "$image"
    status=0
    "$docker" run --rm ${platform[@]+"${platform[@]}"} \
        -v "$source_dir:/src:ro" -v "$package_dir:/package:ro" -v "$out:/out" \
        "$image" bash /src/host/tools/linux-package-test.sh \
        "/package/$package_name" /src /out || status=$?
    if [ "$rmi" = 1 ]; then
        "$docker" image rm "$image" >/dev/null || true
    fi
    exit $status
fi
image=${image:-debian:13}

# The README's Linux package lists, verbatim. Keep these in step with the
# "Linux Prerequisites" section of README.md: testing them is half the point.
packages="build-essential cmake git pkg-config \
  libusb-1.0-0-dev libprotobuf-dev protobuf-compiler libsqlite3-dev \
  qt6-base-dev qt6-svg-dev qt6-declarative-dev"
if [ "$quick3d" = 1 ]; then
    packages="$packages qt6-quick3d-dev \
  qml6-module-qtquick3d qml6-module-qtquick3d-assetutils qml6-module-qtquick3d-helpers"
fi

# Runs inside the container. The checkout belongs to another uid there, so git
# (used by the version helpers) has to be told it is safe.
script='
set -eu
export DEBIAN_FRONTEND=noninteractive
echo "==> installing packages"
apt-get update -qq
apt-get install -y -qq $PACKAGES >/dev/null
git config --global --add safe.directory /src
echo "==> configuring"
status=0
cmake -S /src -B /tmp/build -DBUILD_EMBEDDED=OFF -DBUILD_HOST_DOCS=OFF || status=$?
if [ $status = 0 ]; then
    echo "==> building $TARGET"
    cmake --build /tmp/build --target "$TARGET" --parallel "$(nproc)" || status=$?
fi
echo "==> exit status $status"
if [ "$SHELL_AFTER" = 1 ]; then
    cd /tmp/build 2>/dev/null || true
    bash -i </dev/tty >/dev/tty 2>&1 || true
fi
exit $status
'

tty_args=()
[ "$shell" = 0 ] || tty_args=(-it)

printf '==> %s, target %s, Quick 3D %s\n' "$image" "$target" \
    "$([ "$quick3d" = 1 ] && echo installed || echo left out)"
status=0
"$docker" run --rm ${tty_args[@]+"${tty_args[@]}"} \
    -v "$source_dir:/src:ro" \
    -e PACKAGES="$packages" -e TARGET="$target" -e SHELL_AFTER="$shell" \
    "$image" bash -c "$script" || status=$?

if [ "$rmi" = 1 ]; then
    "$docker" image rm "$image" >/dev/null || true
fi
exit $status
