#!/usr/bin/env bash
# Builds the .deb inside a Debian or Ubuntu container.
# shellcheck source-path=SCRIPTDIR
# shellcheck source=common.sh
source /src/scripts/package/common.sh
. /etc/os-release
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential cmake ninja-build dpkg-dev file ca-certificates \
    qt6-base-dev qt6-base-private-dev libkf6syntaxhighlighting-dev \
    qt6-svg-plugins qt6-image-formats-plugins fonts-dejavu-core desktop-file-utils >/dev/null

prepare_out
rm -rf "$WORK" && mkdir -p "$WORK"
source_tarball "$WORK/src.tar.gz"
tar -C "$WORK" -xzf "$WORK/src.tar.gz"
SRC=$WORK/markdown-glass-$APP_VERSION

# Debian's changelog, generated from CHANGELOG.md, travels with the package.
bash "$SRC/scripts/package/changelog.sh" debian "1~${ID}${VERSION_ID}" | gzip -9n >"$WORK/changelog.Debian.gz"

# The distribution's own hardening and optimisation flags, as debhelper
# would pass them, with every hardening feature on (bindnow is off by
# default). CMake does not read CPPFLAGS, so they join the others.
eval "$(DEB_BUILD_MAINT_OPTIONS=hardening=+all dpkg-buildflags --export=sh)"
export CFLAGS="$CPPFLAGS $CFLAGS" CXXFLAGS="$CPPFLAGS $CXXFLAGS"

cmake -S "$SRC" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr \
    -DMDGLASS_DEBIAN_CHANGELOG="$WORK/changelog.Debian.gz" \
    -DMDGLASS_DEBIAN_COPYRIGHT="$SRC/packaging/copyright"
cmake --build "$WORK/build"
ctest --test-dir "$WORK/build" --output-on-failure
desktop-file-validate "$SRC/data/io.github.markdown_glass.desktop"

# The distribution goes in the file name, so packages for different releases can sit side by side.
(cd "$WORK/build" && cpack -G DEB -D CPACK_DEBIAN_PACKAGE_RELEASE="1~${ID}${VERSION_ID}")
cp "$WORK"/build/*.deb "$OUT/"
dpkg-deb --info "$OUT"/*.deb | sed -n '/Package:/,$p'
ls -la "$OUT"
