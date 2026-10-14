#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_PREFIX="${QT_PREFIX:-$HOME/Qt/6.12.0/gcc_64}"
APPDIR="$ROOT/build/AppDir"
TOOLS="$ROOT/build/appimage-tools"
mkdir -p "$TOOLS" "$ROOT/dist"
fetch() {
    local name="$1" repo="$2" digest="$3"
    curl --fail --location --retry 3 "https://github.com/$repo/releases/download/continuous/$name" -o "$TOOLS/$name"
    echo "$digest  $TOOLS/$name" | sha256sum --check -
    chmod +x "$TOOLS/$name"
}
fetch linuxdeploy-x86_64.AppImage linuxdeploy/linuxdeploy 8aea8da0f7f7039d2a2cecb14657d752a222a5e1d3825caeef186c82f751cdd1
fetch linuxdeploy-plugin-qt-x86_64.AppImage linuxdeploy/linuxdeploy-plugin-qt cfc1055b2b9dbc08412b579f20990b7b41a17b61beaa5847dc9477c96c9e9617
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/plugins/wayland-shell-integration" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$APPDIR/usr/share/jot-linux"
cp "$ROOT/build/jot-linux" "$APPDIR/usr/bin/"
cp "$ROOT/build/plugins/wayland-shell-integration/liblayer-shell.so" "$APPDIR/usr/plugins/wayland-shell-integration/"
cp "$ROOT/resources/icons/jot.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/jot-linux.png"
cp "$ROOT/scripts/setup-permissions.sh" "$APPDIR/usr/share/jot-linux/"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$APPDIR/usr/share/jot-linux/"
cp -r "$ROOT/resources/fonts" "$APPDIR/usr/share/jot-linux/font-licenses"
rm "$APPDIR/usr/share/jot-linux/font-licenses/"*.ttf
cp "$ROOT/resources/icons/breeze/LICENSE.txt" "$APPDIR/usr/share/jot-linux/breeze-LICENSE.txt"
cp -r "$ROOT/third_party/layer-shell-qt/LICENSES" "$APPDIR/usr/share/jot-linux/layer-shell-qt-licenses"
cat > "$APPDIR/usr/share/applications/jot-linux.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Jot
Comment=Hold a key. Speak. It types.
Exec=jot-linux %u
Icon=jot-linux
Categories=Utility;Accessibility;
StartupNotify=false
X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input
MimeType=x-scheme-handler/jot;
DESKTOP
export APPIMAGE_EXTRACT_AND_RUN=1
# Jot uses SQLite only. Other Qt SQL drivers require proprietary client libraries.
QT_PLUGIN_STAGE="$TOOLS/qt-plugins"
rm -rf "$QT_PLUGIN_STAGE"
mkdir -p "$QT_PLUGIN_STAGE"
cp -a "$QT_PREFIX/plugins/." "$QT_PLUGIN_STAGE/"
find "$QT_PLUGIN_STAGE/sqldrivers" -type f ! -name 'libqsqlite.so' -delete
export QT_PREFIX QT_PLUGIN_STAGE
cat > "$TOOLS/qmake" <<'QMAKE'
#!/usr/bin/env bash
set -euo pipefail
"$QT_PREFIX/bin/qmake" "$@" | sed "s|$QT_PREFIX/plugins|$QT_PLUGIN_STAGE|g"
QMAKE
chmod +x "$TOOLS/qmake"
export QMAKE="$TOOLS/qmake"
export QML_SOURCES_PATHS="$ROOT/resources/qml"
export EXTRA_PLATFORM_PLUGINS='libqwayland.so;libqoffscreen.so'
export LD_LIBRARY_PATH="$QT_PREFIX/lib:$ROOT/third_party/install/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Qt Basic style and dynamically selected SQL/audio plugins must be included.
export EXTRA_QT_MODULES='sql;multimedia;svg;waylandclient'
"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --executable "$APPDIR/usr/bin/jot-linux" --library "$APPDIR/usr/plugins/wayland-shell-integration/liblayer-shell.so" --desktop-file "$APPDIR/usr/share/applications/jot-linux.desktop" --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/jot-linux.png" --plugin qt
# The program searches for custom plugins beside its executable.
mkdir -p "$APPDIR/usr/bin/plugins/wayland-shell-integration"
cp "$APPDIR/usr/plugins/wayland-shell-integration/liblayer-shell.so" "$APPDIR/usr/bin/plugins/wayland-shell-integration/"
cd "$ROOT/dist"
export OUTPUT="Jot-Linux-$(git -C "$ROOT" rev-parse --short HEAD)-x86_64.AppImage"
"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --output appimage
sha256sum "$OUTPUT" > "$OUTPUT.sha256"
