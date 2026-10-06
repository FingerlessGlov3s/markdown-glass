#!/usr/bin/env bash
# Runs the libFuzzer targets in podman for a short while each.
# Usage: scripts/fuzz.sh [seconds-per-target]   (default 60)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SECONDS_EACH="${1:-60}"
if [[ ! "$SECONDS_EACH" =~ ^[0-9]+$ ]]; then
    echo "usage: $0 [seconds-per-target]" >&2
    exit 2
fi

# Values reach the container through its environment, never spliced into the script.
# shellcheck disable=SC2016 # single quotes on purpose: the body is expanded by the shell in the container
exec "$HERE/container.sh" env SECONDS_EACH="$SECONDS_EACH" bash -c '
    set -e
    CC=clang CXX=clang++ cmake -S . -B build/fuzz -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DMDGLASS_FUZZ=ON -DBUILD_TESTING=OFF
    targets=(html markdown textcheck links svgcheck publicaddress)
    cmake --build build/fuzz --target "${targets[@]/#/fuzz_}"
    for t in "${targets[@]}"; do mkdir -p "build/fuzz/corpus-$t"; done
    # Seeds from the test samples; find, so a missing kind of sample is not an error.
    seed() { find tests/samples -maxdepth 1 -type f -name "$1" -exec cp -t "build/fuzz/corpus-$2" {} +; }
    seed "*.md" markdown
    seed "*.md" html
    seed "*" textcheck
    seed "*.svg" svgcheck
    printf "%s" "other.md#usage" >build/fuzz/corpus-links/relative
    printf "%s" "mailto:a@example.com?subject=x&attach=/etc/passwd" >build/fuzz/corpus-links/mailto
    printf "%s" "https://example.com/a?b#c" >build/fuzz/corpus-links/web
    printf "%s" "::ffff:127.0.0.1" >build/fuzz/corpus-publicaddress/mapped
    printf "%s" "64:ff9b::808:808" >build/fuzz/corpus-publicaddress/nat64

    run() {
        local target=$1 max_len=$2
        build/fuzz/fuzz/fuzz_$target -max_total_time="$SECONDS_EACH" -max_len="$max_len" -timeout=10 \
            "build/fuzz/corpus-$target"
    }
    run html 4096
    run markdown 8192
    run textcheck 70000
    run links 2048
    run svgcheck 8192
    run publicaddress 128
'
