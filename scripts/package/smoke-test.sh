#!/usr/bin/env bash
# Installs the freshly built package into a clean container of the same
# distribution and renders a document with it. This proves the package's
# dependencies resolve and the installed program starts.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive QT_QPA_PLATFORM=offscreen HOME=/tmp XDG_CONFIG_HOME=/tmp/cfg
OUT=/src/dist/$TARGET
. /etc/os-release

case "$TARGET" in
    appimage)
        # A plain desktop library set, no Qt: the AppImage must bring its own.
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends libfontconfig1 libfreetype6 libdbus-1-3 \
            libxkbcommon0 libx11-6 fonts-dejavu-core >/dev/null
        # The checkout is mounted read-only here, so run a copy.
        app=/tmp/$(basename "$(ls "$OUT"/*.AppImage)")
        cp "$OUT"/*.AppImage "$app"
        chmod +x "$app"
        run=("$app" --appimage-extract-and-run)
        ;;
    fedora-*)
        dnf -y -q install "$OUT"/*.rpm
        run=(markdown-glass) ;;
    almalinux-*)
        dnf -y -q install epel-release dnf-plugins-core
        dnf config-manager --set-enabled crb
        dnf -y -q install "$OUT"/*.rpm
        run=(markdown-glass) ;;
    opensuse-*)
        # Minimal openSUSE images have no fonts at all; a desktop always does.
        zypper -q --non-interactive install dejavu-fonts >/dev/null
        # --allow-unsigned-rpm covers the local package; repository packages
        # it pulls in are still signature-checked.
        zypper -q --non-interactive install --allow-unsigned-rpm "$OUT"/*.rpm
        run=(markdown-glass) ;;
    debian-*|ubuntu-*)
        apt-get update -qq
        apt-get install -y -qq "$OUT"/*.deb >/dev/null
        run=(markdown-glass) ;;
    *) echo "No smoke test for $TARGET" >&2; exit 1 ;;
esac

"${run[@]}" --version
"${run[@]}" --render-png /tmp/showcase.png --width 900 /src/docs/showcase.md
size=$(stat -c %s /tmp/showcase.png)
echo "Rendered docs/showcase.md to a ${size}-byte PNG"
# A correct render is well over 100 KB. A blank page (no fonts, nothing
# drawn) compresses to under 10 KB, so this catches "starts but shows nothing".
(( size > 20000 ))
