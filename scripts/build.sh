#!/usr/bin/env bash
# Builds Markdown Glass in the podman build container.
#   scripts/build.sh        -> build/default/src/markdown-glass
#   scripts/build.sh rpm    -> dist/fedora-44/*.rpm (same as scripts/package.sh fedora-44)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
case "$BUILD_TYPE" in
    Debug | Release | RelWithDebInfo | MinSizeRel) ;;
    *)
        echo "BUILD_TYPE must be Debug, Release, RelWithDebInfo or MinSizeRel" >&2
        exit 2
        ;;
esac

if [[ "${1:-}" == "rpm" ]]; then
    exec "$HERE/package.sh" fedora-44
fi

# shellcheck disable=SC2016 # single quotes on purpose: the body is expanded by the shell in the container
exec "$HERE/container.sh" env BUILD_TYPE="$BUILD_TYPE" bash -c '
    cmake -S . -B build/default -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" &&
    cmake --build build/default'
