#!/usr/bin/env bash
# Builds, tests and packages Markdown Glass for one target inside a container,
# exactly as the GitHub Actions workflow does. Output goes to dist/<target>/.
#
#   scripts/package.sh <target>          e.g. fedora-44, debian-13, appimage
#   scripts/package.sh all               every target, two at a time (logs in build/logs/)
#   scripts/package.sh list              show the targets
#
# Uses podman if installed, otherwise docker (set CONTAINER_ENGINE to choose).
# Builds for the host's CPU.
#
# Each container is capped so a build cannot exhaust the machine:
#   JOBS=4          parallel compile jobs inside the container
#   MEMORY_GB=8     memory limit; the build is killed rather than the desktop
#                   (6 per container when "all" runs two at once)
#   PARALLEL=2      how many targets "all" builds at the same time
# A single target is refused if less than MEMORY_GB + 2 GB is free when it
# starts; "all" waits for memory to free up before starting the next one.
#
# The containers get network (package managers need it) but no privileges
# beyond what installing packages takes; see container_opts.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# target  image  build script
TARGETS="
fedora-43           registry.fedoraproject.org/fedora:43        build-rpm.sh
fedora-44           registry.fedoraproject.org/fedora:44        build-rpm.sh
almalinux-10        docker.io/library/almalinux:10              build-rpm.sh
opensuse-tumbleweed registry.opensuse.org/opensuse/tumbleweed   build-rpm.sh
opensuse-leap-16    registry.opensuse.org/opensuse/leap:16.0    build-rpm.sh
debian-13           docker.io/library/debian:13                 build-deb.sh
ubuntu-26.04        docker.io/library/ubuntu:26.04              build-deb.sh
ubuntu-26.10        docker.io/library/ubuntu:26.10              build-deb.sh
appimage            docker.io/library/ubuntu:22.04              build-appimage.sh
"

engine=${CONTAINER_ENGINE:-$(command -v podman || command -v docker || true)}
[[ -n "$engine" ]] || { echo "podman or docker is required" >&2; exit 1; }

JOBS=${JOBS:-4}
PARALLEL=${PARALLEL:-2}
if [[ "${1:-}" == all && $PARALLEL -gt 1 ]]; then
    MEMORY_GB=${MEMORY_GB:-6}
else
    MEMORY_GB=${MEMORY_GB:-8}
fi
for setting in JOBS PARALLEL MEMORY_GB; do
    if [[ ! "${!setting}" =~ ^[1-9][0-9]*$ ]]; then
        echo "$setting must be a positive number" >&2
        exit 2
    fi
done

free_memory_gb() {
    awk '/^MemAvailable:/ {printf "%d", $2 / 1048576}' /proc/meminfo
}

check_memory() {
    [[ -r /proc/meminfo ]] || return 0
    # CI runners are throwaway machines with nothing else to protect, and
    # their free memory is not the whole story (container.sh skips its cap
    # there for the same reason).
    [[ -z "${CI:-}" ]] || return 0
    local free_gb
    free_gb=$(free_memory_gb)
    if (( free_gb < MEMORY_GB + 2 )); then
        echo "Only ${free_gb} GB of memory is free; need $((MEMORY_GB + 2)) GB to start a build" \
             "(MEMORY_GB=$MEMORY_GB). Close something or lower MEMORY_GB and JOBS." >&2
        exit 3
    fi
    echo "    ${free_gb} GB free; container limited to ${MEMORY_GB} GB and ${JOBS} jobs"
}

rootless_podman() {
    [[ "$(basename "$engine")" == podman ]] &&
        [[ "$("$engine" info --format '{{.Host.Security.Rootless}}' 2>/dev/null)" == true ]]
}

# The options every container here runs with: the build and the smoke test
# both run downloaded tools and package managers as the container's root, so
# the checkout is mounted read-only and the container gets as little as
# installing packages takes.
container_opts() { # target
    local target=$1
    local opts=(--rm -v "$ROOT:/src:ro" -w /src -e TARGET="$target"
        --memory "${MEMORY_GB}g" --memory-swap "${MEMORY_GB}g"
        # A fork bomb in a test or a runaway build tool is contained.
        --pids-limit 4096
        # Nothing in the container may gain privileges through setuid binaries.
        --security-opt no-new-privileges
        # Drop every capability, then add back only what package managers
        # need: apt downloads as the _apt user (SETUID, SETGID; SETPCAP to
        # shed what it no longer needs), dnf, zypper and rpm set file owners,
        # modes and capabilities when installing (CHOWN, DAC_OVERRIDE, FOWNER,
        # FSETID, SETFCAP), and KILL lets the package managers' helpers stop
        # each other. Network stays on: the package managers need it.
        --cap-drop=all
        --cap-add=CHOWN --cap-add=DAC_OVERRIDE --cap-add=FOWNER --cap-add=FSETID
        --cap-add=SETUID --cap-add=SETGID --cap-add=SETPCAP --cap-add=SETFCAP --cap-add=KILL
        # The git directory is hidden behind an empty tmpfs: the source
        # tarball (common.sh) excludes it anyway, and nothing in the build
        # needs it. (Needs .git to be a directory, as in a plain clone and
        # on CI; a worktree's .git file cannot be mounted over.)
        --tmpfs /src/.git)
    # SELinux labelling would otherwise deny the bind mounts under podman.
    [[ "$(basename "$engine")" == podman ]] && opts+=(--security-opt label=disable)
    printf '%s\n' "${opts[@]}"
}

run_target() {
    local target=$1 line image script
    line=$(awk -v t="$target" '$1 == t' <<<"$TARGETS")
    [[ -n "$line" ]] || { echo "Unknown target: $target (try: $0 list)" >&2; exit 2; }
    read -r _ image script <<<"$line"

    echo "==> $target ($image)"
    check_memory
    # Only dist/ (and the AppImage cache) can be written.
    mkdir -p "$ROOT/dist"
    local opts=()
    mapfile -t opts < <(container_opts "$target")
    opts+=(-v "$ROOT/dist:/src/dist" -e JOBS="$JOBS" -e CMAKE_BUILD_PARALLEL_LEVEL="$JOBS")
    # Under a rootful engine, files written as the container's root must be
    # handed back; under rootless podman they are the invoking user's already,
    # and chown to the host uid would pick a subordinate id instead.
    rootless_podman || opts+=(-e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)")
    # The AppImage's self-built Qt is cached between runs.
    if [[ "$target" == appimage ]]; then
        mkdir -p "$ROOT/.cache/appimage"
        opts+=(-v "$ROOT/.cache/appimage:/cache")
    fi
    "$engine" run "${opts[@]}" "$image" bash "scripts/package/$script"

    echo "==> $target: install and run in a clean container"
    local test_image=$image
    # The AppImage must also run where no distribution package can: Ubuntu 24.04.
    [[ "$target" == appimage ]] && test_image=docker.io/library/ubuntu:24.04
    mapfile -t opts < <(container_opts "$target")
    "$engine" run "${opts[@]}" "$test_image" bash scripts/package/smoke-test.sh
}

case "${1:-}" in
    list) awk 'NF {print $1}' <<<"$TARGETS" ;;
    all)
        # A few targets at a time, each with its own log. Before starting the
        # next one, wait until there is room for another container's limit.
        mkdir -p "$ROOT/build/logs"
        declare -A pids=()
        failed=()
        reap() {
            local t
            for t in "${!pids[@]}"; do
                if ! kill -0 "${pids[$t]}" 2>/dev/null; then
                    if wait "${pids[$t]}"; then echo "    $t: ok"; else echo "    $t: FAILED (build/logs/$t.log)"; failed+=("$t"); fi
                    unset "pids[$t]"
                fi
            done
        }
        while read -r t; do
            while (( ${#pids[@]} >= PARALLEL )) || { [[ -r /proc/meminfo ]] && (( $(free_memory_gb) < MEMORY_GB + 2 )) && (( ${#pids[@]} > 0 )); }; do
                sleep 5
                reap
            done
            echo "==> starting $t (log: build/logs/$t.log)"
            # No stdin: the loop's input is the target list.
            run_target "$t" >"$ROOT/build/logs/$t.log" 2>&1 </dev/null &
            pids[$t]=$!
        done < <(awk 'NF {print $1}' <<<"$TARGETS")
        while (( ${#pids[@]} > 0 )); do sleep 5; reap; done
        if (( ${#failed[@]} )); then echo "Failed: ${failed[*]}"; exit 1; fi
        echo "All targets built."
        ;;
    "") sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
    *) run_target "$1" ;;
esac
