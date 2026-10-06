#!/usr/bin/env bash
# Builds the RPM inside a Fedora, AlmaLinux or openSUSE container.
# shellcheck source-path=SCRIPTDIR
# shellcheck source=common.sh
source /src/scripts/package/common.sh
. /etc/os-release

SPEC=$WORK/markdown-glass.spec
render_spec "$SPEC"

case "$ID" in
    fedora)
        DIST=".fc$VERSION_ID"
        dnf -y -q install rpm-build dnf-plugins-core rpmlint annobin-annocheck appstream dejavu-sans-fonts dejavu-sans-mono-fonts
        dnf -y -q builddep "$SPEC"
        ;;
    almalinux)
        DIST=".el${VERSION_ID%%.*}"
        # KF6 comes from EPEL; EPEL in turn needs CodeReady Builder (CRB).
        dnf -y -q install epel-release dnf-plugins-core
        dnf config-manager --set-enabled crb
        dnf -y -q install rpm-build rpmlint dejavu-sans-fonts dejavu-sans-mono-fonts
        dnf -y -q builddep "$SPEC"
        ;;
    opensuse-tumbleweed|opensuse-leap)
        if [[ "$ID" == opensuse-tumbleweed ]]; then DIST=".tw"; else DIST=".leap${VERSION_ID%%.*}"; fi
        zypper -q --non-interactive refresh
        zypper -q --non-interactive install rpm-build rpmlint dejavu-fonts tar gzip \
            cmake gcc-c++ desktop-file-utils libQt6Svg6 \
            'cmake(Qt6Core)' 'cmake(Qt6Gui)' 'cmake(Qt6Widgets)' 'cmake(Qt6PrintSupport)' \
            'cmake(Qt6Network)' 'cmake(Qt6Test)' 'cmake(KF6SyntaxHighlighting)'
        ;;
    *)
        echo "Unsupported distribution: $ID" >&2
        exit 1
        ;;
esac

prepare_out
TOP=$WORK/rpm
rm -rf "$TOP" && mkdir -p "$TOP/SOURCES"
source_tarball "$TOP/SOURCES/markdown-glass-$APP_VERSION.tar.gz"

# %check in the spec runs the full test suite.
rpmbuild -bb --define "_topdir $TOP" --define "dist $DIST" --define "_smp_ncpus_max $JOBS" \
    "$SPEC"

find "$TOP/RPMS" -name "markdown-glass-$APP_VERSION-*.rpm" ! -name '*debug*' -exec cp {} "$OUT/" \;
# Reported, not enforced: each distribution's rpmlint has its own policy
# findings (openSUSE's branding and dependency rules, for example). The known
# false positives are filtered in packaging/markdown-glass.rpmlintrc.
rpmlint -r /src/packaging/markdown-glass.rpmlintrc "$OUT"/*.rpm || true

# On Fedora, check the AppStream metadata (its release list is generated from
# CHANGELOG.md) is valid as software centres will read it.
if [[ "$ID" == fedora ]]; then
    rpm2cpio "$OUT"/markdown-glass-*.rpm | (cd "$WORK" && cpio -idm --quiet './usr/share/metainfo/*')
    appstreamcli validate --no-net --pedantic "$WORK"/usr/share/metainfo/io.github.markdown_glass.metainfo.xml
fi

# On Fedora, confirm the distribution's hardening flags (PIE, RELRO, stack
# protector, fortify, CET) reached the binary. annocheck needs the debuginfo.
# It unpacks into the current directory, and the checkout is read-only.
if [[ "$ID" == fedora ]]; then
    (cd "$WORK" && annocheck --ignore-unknown \
        --debug-rpm="$(find "$TOP/RPMS" -name 'markdown-glass-debuginfo-*.rpm' | head -1)" \
        "$OUT"/markdown-glass-*.rpm)
fi
ls -la "$OUT"
