#!/usr/bin/env bash
# Builds the AppImage inside an Ubuntu 22.04 container.
#
# The AppImage carries its own Qt, so it does not depend on the host's Qt or
# KDE Frameworks. It does depend on the host's glibc, which is why the base is
# deliberately old: the result runs on any distribution with glibc 2.35 or
# newer (Ubuntu 22.04+, Debian 12+, Fedora 36+, ...).
#
# To keep it small, Qt is built from source with only what the viewer uses:
# no ICU (about 30 MB on its own), no OpenGL, no SQL, and only the image
# format plugins the viewer accepts. The build is cached in /cache.
# shellcheck source-path=SCRIPTDIR
# shellcheck source=common.sh
source /src/scripts/package/common.sh
export DEBIAN_FRONTEND=noninteractive

# Every download is pinned and checked against a SHA-256 (or a commit for
# git), so a compromised mirror or release page cannot change what is built.
QT_VERSION=6.11.2
declare -A QT_SHA256=( # from the .sha256 files Qt publishes beside each tarball
    [qtbase]=5b2e00eccaf5a4d8c14134ffa0ea8dfd0a35ae1ffc7f8d87fa4305a1ed23cf22
    [qtsvg]=d594337feca84c26fb67fe87b85e6a5c12fda404b611d905f9d138210c311876
    [qtimageformats]=cecd8900f34b6550076309bc94f62f828008b633a4239e0a08c86788f41001f8
    [qtwayland]=8eb7615e39332a10f506e8dd70f02d5954bb5949ff54f6dcbf8bd6168222f9df
)
KF_VERSION=6.30.0
declare -A KF_COMMIT=( # what the v$KF_VERSION tags point to
    [extra-cmake-modules]=68483132b87f4d7b953aec94ef51d1234d41f937
    [syntax-highlighting]=065ef8580096caea079fddc22d420c786cf53f92
)
# The AppImage tools, at fixed releases rather than their moving "continuous" builds.
LINUXDEPLOY=linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy
LINUXDEPLOY_QT=linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt
APPIMAGETOOL=AppImage/appimagetool/releases/download/1.9.1/appimagetool
declare -A TOOL_SHA256=(
    [linuxdeploy-x86_64]=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
    [linuxdeploy-aarch64]=620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff
    [linuxdeploy-plugin-qt-x86_64]=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
    [linuxdeploy-plugin-qt-aarch64]=bf1c24aff6d749b5cf423afad6f15abd4440f81dec1aab95706b25f6667cdcf1
    [appimagetool-x86_64]=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
    [appimagetool-aarch64]=f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158
)

ARCH=$(uname -m) # x86_64 or aarch64
CACHE=/cache
mkdir -p "$CACHE/downloads"

apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential ninja-build perl python3 python3-pip pkg-config git curl ca-certificates \
    xz-utils file desktop-file-utils fonts-dejavu-core \
    libfontconfig1-dev libfreetype-dev libdbus-1-dev libssl-dev libcups2-dev zlib1g-dev \
    libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev libxrender-dev \
    libxcb1-dev libxcb-cursor-dev libxcb-glx0-dev libxcb-keysyms1-dev libxcb-image0-dev \
    libxcb-shm0-dev libxcb-icccm4-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-shape0-dev \
    libxcb-randr0-dev libxcb-render-util0-dev libxcb-util-dev libxcb-xinerama0-dev \
    libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev \
    libwayland-dev libwayland-bin >/dev/null
# KSyntaxHighlighting needs a newer CMake than Ubuntu 22.04 ships.
# Not silenced: a hash mismatch must be visible in the log.
pip3 install -q --require-hashes -r /dev/stdin <<'REQUIREMENTS'
cmake==3.31.10 \
    --hash=sha256:3c17bb24dba15f8ecc3fd706afe04264410ef88796f4115c119327c961d5dc57 \
    --hash=sha256:2f766bb46367e5e0559fa33184653754bce044583a06014dcaebf8e6dff8a1f1 \
    --hash=sha256:a4a5615f31f692c9b9aa8b365704e4b76172348af6fa40e16fea3f118bb01194
REQUIREMENTS
hash -r
cmake --version | head -1

# Downloads `url` into `dir` (the download cache by default) unless a copy
# with the expected SHA-256 is already there, and prints its path. A download
# goes to a temporary name first, so an interrupted one is never mistaken for
# a complete file, and one with the wrong checksum stops the build.
fetch() { # url sha256 [dir]
    local url=$1 sha256=$2 dir=${3:-$CACHE/downloads} file
    file="$dir/$(basename "$url")"
    mkdir -p "$dir"
    if [[ -f "$file" ]] && echo "$sha256  $file" | sha256sum --check --status; then
        echo "$file"
        return
    fi
    rm -f "$file" "$file.part"
    curl -fsSL --retry 3 -o "$file.part" "$url" >&2
    if ! echo "$sha256  $file.part" | sha256sum --check --status; then
        echo "Checksum mismatch for $url" >&2
        echo "  expected $sha256" >&2
        echo "  got      $(sha256sum "$file.part" | cut -d' ' -f1)" >&2
        rm -f "$file.part"
        return 1
    fi
    mv "$file.part" "$file"
    echo "$file"
}

# The usual distribution hardening, which the RPM and Debian builds get from
# their packaging tools. Exported before anything is compiled so that the
# bundled Qt and KSyntaxHighlighting get it too, not just the viewer.
# _FORTIFY_SOURCE needs optimisation: Qt's -release and -optimize-size, and
# the Release builds below, provide it. Position-independent code is asked
# for through CMake rather than -fPIE/-pie, which would break the shared
# library links in Qt.
export CFLAGS="-D_FORTIFY_SOURCE=2 -fstack-protector-strong -fstack-clash-protection"
export CXXFLAGS=$CFLAGS
export LDFLAGS="-Wl,-z,relro,-z,now"
PIE_OPTION=-DCMAKE_POSITION_INDEPENDENT_CODE=ON

# ------------------------------------------------------------------ Qt
QT_OPTIONS="-release -optimize-size -shared -no-icu -no-opengl -no-feature-vulkan -no-glib
    -no-feature-sql -openssl-runtime -dbus-runtime -fontconfig -system-freetype
    -qt-harfbuzz -qt-libpng -qt-libjpeg -qt-pcre -system-zlib -xcb -xkbcommon -cups
    -nomake examples -nomake tests"
# Of the extra image formats only WebP is accepted by the viewer.
IMAGEFORMATS_OPTIONS="-DFEATURE_tiff=OFF -DFEATURE_mng=OFF -DFEATURE_jasper=OFF -DFEATURE_wbmp=OFF
    -DFEATURE_tga=OFF -DFEATURE_icns=OFF -DFEATURE_macheif=OFF -DFEATURE_system_webp=OFF"
# The compiler flags are part of the key, so a cached Qt built with different
# hardening is not reused.
QT_KEY=$(echo "$QT_VERSION $QT_OPTIONS $IMAGEFORMATS_OPTIONS $PIE_OPTION $CFLAGS $CXXFLAGS $LDFLAGS" | sha1sum | cut -c1-10)
QT=$CACHE/qt-$QT_VERSION-$ARCH-$QT_KEY
# Builds from earlier settings are of no further use.
find "$CACHE" -maxdepth 1 \( -name 'qt-*' -o -name 'kf-*' \) ! -name "*-$QT_KEY" -exec rm -rf {} +

if [[ ! -x "$QT/bin/qmake" ]]; then
    echo "==> Building Qt $QT_VERSION (cached afterwards)"
    rm -rf "$WORK/qt" && mkdir -p "$WORK/qt"
    base=https://download.qt.io/official_releases/qt/${QT_VERSION%.*}/$QT_VERSION/submodules
    for module in qtbase qtsvg qtimageformats qtwayland; do
        tarball=$(fetch "$base/$module-everywhere-src-$QT_VERSION.tar.xz" "${QT_SHA256[$module]}")
        tar -C "$WORK/qt" -xf "$tarball"
    done
    mkdir -p "$WORK/qt/build-qtbase"
    # Everything after -- goes to CMake. QT_OPTIONS is split on purpose.
    # shellcheck disable=SC2086
    (cd "$WORK/qt/build-qtbase" &&
        ../qtbase-everywhere-src-$QT_VERSION/configure -prefix "$QT" $QT_OPTIONS -- "$PIE_OPTION" >/dev/null &&
        cmake --build . --parallel "$JOBS" >/dev/null && cmake --install . >/dev/null)
    for module in qtsvg qtimageformats qtwayland; do
        mkdir -p "$WORK/qt/build-$module"
        extra=(-- "$PIE_OPTION")
        # shellcheck disable=SC2206
        [[ $module == qtimageformats ]] && extra+=($IMAGEFORMATS_OPTIONS)
        (cd "$WORK/qt/build-$module" &&
            "$QT/bin/qt-configure-module" "../$module-everywhere-src-$QT_VERSION" "${extra[@]}" >/dev/null &&
            cmake --build . --parallel "$JOBS" >/dev/null && cmake --install . >/dev/null)
    done
    rm -rf "$WORK/qt"
fi

# ------------------------------------------------------ KSyntaxHighlighting
# Built as a static library and linked into the viewer.
KF=$CACHE/kf-$KF_VERSION-$ARCH-$QT_KEY
if [[ ! -f "$KF/.done" ]]; then
    echo "==> Building KSyntaxHighlighting $KF_VERSION (cached afterwards)"
    rm -rf "$WORK/kf" "$KF" && mkdir -p "$WORK/kf"
    for repo in extra-cmake-modules syntax-highlighting; do
        git -c advice.detachedHead=false clone -q --depth 1 --branch "v$KF_VERSION" \
            "https://invent.kde.org/frameworks/$repo.git" "$WORK/kf/$repo"
        # A tag can be moved; the commit it named when this was written cannot.
        commit=$(git -C "$WORK/kf/$repo" rev-parse HEAD)
        if [[ "$commit" != "${KF_COMMIT[$repo]}" ]]; then
            echo "$repo v$KF_VERSION is at $commit, expected ${KF_COMMIT[$repo]}" >&2
            exit 1
        fi
        # The library's own translations are never shown by the viewer, and
        # building them needs Qt's translation tools, which are not built.
        rm -rf "$WORK/kf/$repo/po" "$WORK/kf/$repo/poqm"
        cmake -S "$WORK/kf/$repo" -B "$WORK/kf/build-$repo" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$KF" "$PIE_OPTION" \
            -DCMAKE_PREFIX_PATH="$QT;$KF" -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
            -DBUILD_QCH=OFF -DBUILD_DOC=OFF -DQT_MAJOR_VERSION=6 >/dev/null
        cmake --build "$WORK/kf/build-$repo" --parallel "$JOBS" >/dev/null
        cmake --install "$WORK/kf/build-$repo" >/dev/null
    done
    touch "$KF/.done"
    rm -rf "$WORK/kf"
fi

# ------------------------------------------------------------ the viewer
echo "==> Building Markdown Glass $APP_VERSION"
prepare_out
rm -rf "$WORK/app" && mkdir -p "$WORK/app"
source_tarball "$WORK/app/src.tar.gz"
tar -C "$WORK/app" -xzf "$WORK/app/src.tar.gz"
SRC=$WORK/app/markdown-glass-$APP_VERSION
BUILD=$WORK/app/build
APPDIR=$WORK/app/AppDir

cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_PREFIX_PATH="$QT;$KF" "$PIE_OPTION"
cmake --build "$BUILD" --parallel "$JOBS"
ctest --test-dir "$BUILD" --output-on-failure
DESTDIR="$APPDIR" cmake --install "$BUILD" --strip

# The install above puts the viewer's and the parser's licences in the
# AppDir's doc directory; a notice for the libraries bundled only here (the
# distribution packages use the system's) goes beside them, saying where the
# sources at exactly these versions are.
DOCDIR=$APPDIR/usr/share/doc/markdown-glass
[[ -f "$DOCDIR/LICENSE" && -f "$DOCDIR/LICENSE.cmark-gfm" ]] ||
    { echo "The licence files did not land in $DOCDIR" >&2; exit 1; }
cat >"$DOCDIR/THIRD-PARTY-NOTICES" <<NOTICES
This AppImage bundles the following libraries, built from source by
scripts/package/build-appimage.sh in the Markdown Glass repository
(https://github.com/FingerlessGlov3s/markdown-glass):

Qt $QT_VERSION (qtbase, qtsvg, qtimageformats, qtwayland)
  Licence: GNU Lesser General Public License version 3 (LGPL-3.0-only)
  Source:  https://download.qt.io/official_releases/qt/${QT_VERSION%.*}/$QT_VERSION/submodules/
  Built with the options listed in that script; the libraries are shared
  objects under usr/lib and can be replaced with your own build.

KSyntaxHighlighting $KF_VERSION and extra-cmake-modules $KF_VERSION (KDE Frameworks)
  Licence: MIT for the library; its syntax definition files carry their own
  licences, stated in each file.
  Source:  https://invent.kde.org/frameworks/syntax-highlighting (tag v$KF_VERSION)
           https://invent.kde.org/frameworks/extra-cmake-modules (tag v$KF_VERSION)

The licences of Markdown Glass itself and of the cmark-gfm parser compiled
into it are in LICENSE and LICENSE.cmark-gfm in this directory.
NOTICES

# ------------------------------------------------------------ AppImage
TOOLS=$CACHE/tools-$ARCH
for tool in "$LINUXDEPLOY" "$LINUXDEPLOY_QT" "$APPIMAGETOOL"; do
    file=$(fetch "https://github.com/$tool-$ARCH.AppImage" "${TOOL_SHA256[$(basename "$tool")-$ARCH]}" "$TOOLS")
    chmod +x "$file"
done
# Containers have no FUSE, so the tools unpack themselves instead of mounting.
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH=$TOOLS:$PATH
ln -sf "linuxdeploy-plugin-qt-$ARCH.AppImage" "$TOOLS/linuxdeploy-plugin-qt"

export QMAKE=$QT/bin/qmake
export LD_LIBRARY_PATH=$QT/lib
# Native Wayland as well as X11, and offscreen for --render-png. The generic
# Wayland plugin is wanted, not the EGL or Broadcom variants.
wayland_plugin=
for plugin in "$QT/plugins/platforms"/libqwayland*.so; do
    plugin=$(basename "$plugin")
    case $plugin in *egl* | *brcm*) continue ;; esac
    wayland_plugin=$plugin
    break
done
export EXTRA_PLATFORM_PLUGINS="$wayland_plugin;libqoffscreen.so"

"linuxdeploy-$ARCH.AppImage" --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/io.github.markdown_glass.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/io.github.markdown_glass.svg" \
    --plugin qt

# Drop plugins the viewer never uses. It only accepts PNG (built into Qt),
# JPEG, GIF, WebP and SVG.
find "$APPDIR/usr/plugins/imageformats" -name '*.so' \
    ! -name libqjpeg.so ! -name libqgif.so ! -name libqwebp.so ! -name libqsvg.so -delete
rm -rf "$APPDIR/usr/plugins/iconengines" "$APPDIR/usr/plugins/generic" "$APPDIR/usr/plugins/networkinformation" \
    "$APPDIR/usr/plugins/platforminputcontexts/libibusplatforminputcontextplugin.so" 2>/dev/null || true
rm -rf "$APPDIR/usr/translations"

# libcups is left to the host: every desktop that prints has it, and bundling
# it drags in a TLS stack, Kerberos, Avahi and systemd. Without it, printing
# to PDF still works. (linuxdeploy's --exclude-library does not reach the
# libraries its Qt plugin deploys, so it is removed here.)
rm -f "$APPDIR"/usr/lib/libcups.so*
# Likewise the D-Bus client library, which every Linux desktop has and which
# would otherwise bring systemd's library and its compressors along.
rm -f "$APPDIR"/usr/lib/libdbus-1.so*

# Drop every bundled library that nothing left in the AppImage needs.
prune_libraries() {
    local -A keep=()
    local queue=() file lib
    while IFS= read -r file; do queue+=("$file"); done < <(
        find "$APPDIR/usr/bin" "$APPDIR/usr/plugins" -type f \( -name '*.so' -o -perm -u+x \))
    while ((${#queue[@]})); do
        file=${queue[0]}; queue=("${queue[@]:1}")
        for lib in $(readelf -d "$file" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
            if [[ -z "${keep[$lib]:-}" && -e "$APPDIR/usr/lib/$lib" ]]; then
                keep[$lib]=1
                queue+=("$APPDIR/usr/lib/$lib")
            fi
        done
    done
    for file in "$APPDIR"/usr/lib/*.so*; do
        lib=$(basename "$file")
        [[ -n "${keep[$lib]:-}" ]] || { echo "    pruned $lib"; rm -f "$file"; }
    done
}
prune_libraries

NAME="Markdown_Glass-$APP_VERSION-$ARCH.AppImage"
# appimagetool reads the target architecture from the environment.
env ARCH="$ARCH" "appimagetool-$ARCH.AppImage" --no-appstream \
    --comp zstd --mksquashfs-opt -Xcompression-level --mksquashfs-opt 22 \
    "$APPDIR" "$OUT/$NAME"

echo "==> Contents"
(cd "$APPDIR" && find usr/lib usr/plugins -name '*.so*' -printf '%s\t%p\n' | sort -rn | numfmt --to=iec --field=1 | head -40)
ls -la "$OUT"
