#!/usr/bin/env bash
# Regenerates docs/images/screenshot.png from docs/showcase.md using the real window,
# rendered offscreen with the Breeze style.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$HERE/build.sh"
# shellcheck disable=SC2016 # single quotes on purpose: the body is expanded by the shell in the container
exec "$HERE/container.sh" bash -c '
    export XDG_CONFIG_HOME=$(mktemp -d)
    build/default/src/markdown-glass -style breeze --screenshot docs/images/screenshot.png docs/showcase.md'
