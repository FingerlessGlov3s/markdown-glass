#!/usr/bin/env bash
# Runs clang-tidy (.clang-tidy) over src/, then shellcheck over the scripts,
# in the build container. Exits non-zero if either reports anything, or
# clang-tidy fails to analyse a file, so it can gate CI.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JOBS=${JOBS:-4}
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "JOBS must be a positive number" >&2
    exit 2
fi

# shellcheck disable=SC2016 # single quotes on purpose: the body is expanded by the shell in the container
exec "$HERE/container.sh" env JOBS="$JOBS" bash -c '
    set -euo pipefail
    cmake -S . -B build/lint -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF >/dev/null
    # Generated sources (moc, rcc) must exist before they can be analysed.
    cmake --build build/lint --parallel "$JOBS" >/dev/null
    out=build/lint/clang-tidy.txt
    # A file clang-tidy cannot compile, or a crash, must fail the run rather
    # than count as zero findings: keep stderr and the exit status.
    status=0
    find src -name "*.cpp" -print0 | sort -z |
        xargs -0 -P "$JOBS" -n 4 clang-tidy -p build/lint --quiet >"$out" 2>&1 || status=$?
    findings=$(grep -c ": warning:" "$out" || true)
    errors=$(grep -c ": error:" "$out" || true)
    grep -A2 -E ": (warning|error):" "$out" || true
    echo "clang-tidy: $findings finding(s), $errors error(s); full output in $out"
    if [[ $status -ne 0 ]]; then
        echo "clang-tidy did not finish cleanly (status $status)"
        exit 1
    fi
    # -x follows the sourced common.sh. Nothing is silenced here: a script
    # that needs an exception carries a directive with its reason.
    shell_status=0
    shellcheck -x scripts/*.sh scripts/package/*.sh || shell_status=$?
    echo "shellcheck: status $shell_status"
    [[ $findings -eq 0 && $errors -eq 0 && $shell_status -eq 0 ]]
'
