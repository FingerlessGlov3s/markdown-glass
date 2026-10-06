#!/usr/bin/env bash
# Formats the C++ sources with clang-format (.clang-format), in the build
# container so everyone uses the same clang-format version.
#   scripts/format.sh           rewrite files in place
#   scripts/format.sh --check   only report files that need formatting
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mode=(-i)
[[ "${1:-}" == "--check" ]] && mode=(--dry-run -Werror)

exec "$HERE/container.sh" bash -c "
    find src tests fuzz \\( -name '*.cpp' -o -name '*.h' \\) -print0 |
        xargs -0 clang-format ${mode[*]}"
