#!/usr/bin/env bash
# Measures how much of the application the tests exercise (line coverage),
# in the build container. Prints a per-file table and the total, writes an
# HTML report to build/coverage/coverage.html, and fails below 85%.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JOBS=${JOBS:-4}
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "JOBS must be a positive number" >&2
    exit 2
fi
WERROR=OFF
[[ -n "${CI:-}" ]] && WERROR=ON

# shellcheck disable=SC2016 # single quotes on purpose: the body is expanded by the shell in the container
exec "$HERE/container.sh" env JOBS="$JOBS" WERROR="$WERROR" bash -c '
    set -e
    export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME=/tmp/cfg
    # A fresh build each time: leftover data from renamed files corrupts the report.
    rm -rf build/coverage
    cmake -S . -B build/coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMDGLASS_WERROR="$WERROR" \
        -DCMAKE_CXX_FLAGS="--coverage -O0" -DCMAKE_EXE_LINKER_FLAGS=--coverage >/dev/null
    cmake --build build/coverage --parallel "$JOBS" >/dev/null
    ctest --test-dir build/coverage --output-on-failure
    gcovr -r . --filter src/ --exclude ".*_autogen.*" build/coverage \
        --sort uncovered-number --html-details build/coverage/coverage.html --print-summary --txt - \
        --fail-under-line 85
'
