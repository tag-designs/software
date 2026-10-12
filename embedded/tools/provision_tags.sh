#!/bin/bash
#
# Program one tag from a released image and test it.
# Run it once per tag. No prompts.
#
#   provision_tags.sh [TARGET] [RELEASE]
#       TARGET   default PresTag
#       RELEASE  default fw-v0.6
#
set -u

TARGET="${1:-PresTag}"
RELEASE="${2:-fw-v0.6}"

IMAGE="$HOME/Research/TagReleases/tag-firmware-$RELEASE/firmware/$TARGET"
FLASH="$HOME/Research/tag-designs/software/embedded/tools/flash_release.py"
TEST="$HOME/Build/tag-designs/software/bin/tag-test"

if ! "$FLASH" "$IMAGE"; then
    echo
    echo "PROGRAMMING FAILED -- not testing."
    exit 1
fi

sleep 3
"$TEST"
