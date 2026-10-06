#!/usr/bin/env bash
# Runs a command inside the build container with the repo mounted at /src.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The tag carries a hash of the Containerfile, so editing it rebuilds the image.
CONTAINERFILE="$ROOT/scripts/Containerfile"
IMAGE="${MDGLASS_IMAGE:-localhost/markdown-glass-build:f44-$(sha256sum "$CONTAINERFILE" | cut -c1-12)}"

if ! podman image exists "$IMAGE"; then
    podman build -t "$IMAGE" -f "$CONTAINERFILE" "$ROOT/scripts"
fi

# Capped so a build cannot exhaust the machine (override with MEMORY_GB). CI
# runners are throwaway machines whose rootless podman may not allow memory
# limits, so the cap is skipped there.
limits=(--memory "${MEMORY_GB:-8}g" --memory-swap "${MEMORY_GB:-8}g")
[[ -n "${CI:-}" ]] && limits=()
# Builds, tests, lint and fuzzing need nothing from the network and no
# privileges; only the image build above downloads packages.
exec podman run --rm "${limits[@]}" \
    --userns=keep-id \
    --network=none \
    --cap-drop=all \
    --security-opt no-new-privileges \
    --security-opt label=disable \
    -e HOME=/tmp \
    -e CMAKE_BUILD_PARALLEL_LEVEL="${JOBS:-4}" \
    -v "$ROOT:/src" -w /src \
    "$IMAGE" "$@"
