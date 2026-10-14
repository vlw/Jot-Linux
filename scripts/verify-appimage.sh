#!/usr/bin/env bash
set -euo pipefail
IMAGE="$(realpath "${1:?Usage: verify-appimage.sh file.AppImage}")"
(cd "$(dirname "$IMAGE")" && sha256sum --check "$(basename "$IMAGE").sha256")
TASK_TMP="$(mktemp -d)"
trap 'rm -rf "$TASK_TMP"' EXIT
cd "$TASK_TMP"
"$IMAGE" --appimage-extract >/dev/null
APPDIR="$TASK_TMP/squashfs-root"
test -f "$APPDIR/usr/bin/plugins/wayland-shell-integration/liblayer-shell.so"
desktop-file-validate "$APPDIR/usr/share/applications/jot-linux.desktop"
# Remove developer Qt paths; the extracted AppImage must provide its own runtime.
unset QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH LD_LIBRARY_PATH
export LD_LIBRARY_PATH="$APPDIR/usr/lib"
while IFS= read -r -d '' file; do
    if file "$file" | grep -q ELF; then
        deps="$(ldd "$file" 2>&1 || true)"
        if [[ "$deps" == *'not found'* ]]; then
            printf '%s\n%s\n' "$file" "$deps" >&2
            exit 1
        fi
    fi
done < <(find "$APPDIR" -type f -name '*.so*' -print0)
export XDG_CONFIG_HOME="$TASK_TMP/config" XDG_DATA_HOME="$TASK_TMP/data" XDG_CACHE_HOME="$TASK_TMP/cache"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
export QT_PLUGIN_PATH="$APPDIR/usr/plugins"
export QML2_IMPORT_PATH="$APPDIR/usr/qml"
timeout 30 "$APPDIR/AppRun" --smoke-test
