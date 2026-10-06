# Shared helpers for the container build scripts. Sourced, not run.
# shellcheck shell=bash
set -euo pipefail

APP_VERSION=$(sed -n 's/^project(markdown-glass VERSION \([0-9.]*\).*/\1/p' /src/CMakeLists.txt)
OUT=/src/dist/$TARGET
# shellcheck disable=SC2034 # used by the scripts that source this file
WORK=/tmp/mdglass-build

# Parallel jobs, set by scripts/package.sh to keep memory use bounded.
JOBS=${JOBS:-4}
export CMAKE_BUILD_PARALLEL_LEVEL=$JOBS
export CTEST_PARALLEL_LEVEL=1

# Tests run without a display.
export QT_QPA_PLATFORM=offscreen
export XDG_CONFIG_HOME=/tmp/mdglass-config
export HOME=/tmp

# Leaves files in the mounted checkout owned by the invoking user rather than
# root. scripts/package.sh sets HOST_UID only for a rootful engine: under
# rootless podman the container's root already is the invoking user.
fix_ownership() {
    if [[ -n "${HOST_UID:-}" && "$(id -u)" == 0 ]]; then
        chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" /src/dist 2>/dev/null || true
        [[ -d /cache ]] && chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" /cache 2>/dev/null || true
    fi
}
trap fix_ownership EXIT

# A clean copy of the sources, so builds never touch the checkout's build dirs.
source_tarball() {
    local dest=$1
    mkdir -p "$(dirname "$dest")"
    tar -C /src -czf "$dest" \
        --exclude='./build*' --exclude=./dist --exclude=./.cache --exclude=./.git \
        --exclude='./third_party/cmark-gfm/.git' \
        --transform "s,^\.,markdown-glass-$APP_VERSION," .
}

# Fills in packaging/markdown-glass.spec.in: the version from CMakeLists.txt,
# the summary and description from data/description.txt, and the changelog
# from CHANGELOG.md.
render_spec() {
    local dest=$1 spec summary description
    spec=$(</src/packaging/markdown-glass.spec.in)
    # RPM expands macros in the text, so a literal % is written %%.
    summary=$(head -n 1 /src/data/description.txt)
    summary=${summary//%/%%}
    description=$(tail -n +3 /src/data/description.txt)
    description=${description//%/%%}
    spec=$(replace_all "$spec" @VERSION@ "$APP_VERSION")
    spec=$(replace_all "$spec" @SUMMARY@ "$summary")
    spec=$(replace_all "$spec" @DESCRIPTION@ "$description")
    spec=$(replace_all "$spec" @CHANGELOG@ "$(bash /src/scripts/package/changelog.sh rpm)")
    mkdir -p "$(dirname "$dest")"
    printf '%s\n' "$spec" >"$dest"
}

# Prints `text` with every `placeholder` replaced by `value`, taken literally.
# Not ${text//placeholder/value}: from bash 5.2 an & in the value stands for
# the matched text, so "fish & chips" would come out as "fish @X@ chips".
replace_all() {
    local text=$1 placeholder=$2 value=$3 out=
    while [[ $text == *"$placeholder"* ]]; do
        out+=${text%%"$placeholder"*}$value
        text=${text#*"$placeholder"}
    done
    printf '%s' "$out$text"
}

prepare_out() {
    rm -rf "$OUT"
    mkdir -p "$OUT"
}
