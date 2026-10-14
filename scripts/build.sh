#!/usr/bin/env bash
# Build Jot for Linux against Qt 6.12 (~/Qt/6.12.0/gcc_64 by default).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_PREFIX="${QT_PREFIX:-$HOME/Qt/6.12.0/gcc_64}"
TP="$ROOT/third_party"

# layer-shell-qt (KDE, LGPL) must be built against the same Qt as Jot.
if [[ ! -f "$TP/install/lib/x86_64-linux-gnu/plugins/wayland-shell-integration/liblayer-shell.so" ]]; then
    if [[ ! -d "$TP/layer-shell-qt" ]]; then
        git clone --depth 1 --branch v6.7.5 https://github.com/KDE/layer-shell-qt.git "$TP/layer-shell-qt"
    fi
    cmake -S "$TP/layer-shell-qt" -B "$TP/layer-shell-qt/build" -G Ninja \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DCMAKE_INSTALL_PREFIX="$TP/install" \
        -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build "$TP/layer-shell-qt/build"
    cmake --install "$TP/layer-shell-qt/build"
fi

cmake -S "$ROOT" -B "$ROOT/build" -G Ninja -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$ROOT/build"
echo "Built: $ROOT/build/jot-linux"
