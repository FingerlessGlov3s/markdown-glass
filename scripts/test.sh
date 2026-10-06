#!/usr/bin/env bash
# Builds and runs the test suite in podman. Pass --sanitize for ASan + UBSan.
# Under CI, compiler warnings in our own code fail the build.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIR=build/default
ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo"

if [[ "${1:-}" == "--sanitize" ]]; then
    DIR=build/sanitize
    ARGS="-DCMAKE_BUILD_TYPE=Debug -DMDGLASS_SANITIZE=ON"
fi
[[ -n "${CI:-}" ]] && ARGS+=" -DMDGLASS_WERROR=ON"

# shellcheck disable=SC2016 # single quotes on purpose: the body is expanded by the shell in the container
exec "$HERE/container.sh" env DIR="$DIR" ARGS="$ARGS" bash -c '
    set -e
    cmake -S . -B "$DIR" -G Ninja $ARGS
    cmake --build "$DIR"
    ctest --test-dir "$DIR" --output-on-failure'
