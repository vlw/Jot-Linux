#!/usr/bin/env bash
# Install Jot for the current user (no root):
#   ~/.local/opt/jot-linux/            binary + layer-shell plugin + libs
#   ~/.local/bin/jot-linux             launcher symlink
#   ~/.local/share/applications/jot-linux.desktop
#       X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input lets KWin accept Jot's
#       paste keystroke. KWin matches the desktop file by the Exec binary path.
#   ~/.local/share/icons/hicolor/256x256/apps/jot-linux.png
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$HOME/.local/opt/jot-linux"
BIN="$DEST/jot-linux"
[[ -x "$ROOT/build/jot-linux" ]] || "$ROOT/scripts/build.sh"

mkdir -p "$DEST/plugins/wayland-shell-integration" "$DEST/lib" "$DEST/scripts" "$HOME/.local/bin" \
         "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/256x256/apps"
install -m 755 "$ROOT/build/jot-linux" "$BIN.new" && mv -f "$BIN.new" "$BIN"
install -m 644 "$ROOT/build/plugins/wayland-shell-integration/liblayer-shell.so" "$DEST/plugins/wayland-shell-integration/"
cp -a "$ROOT"/third_party/install/lib/x86_64-linux-gnu/libLayerShellQtInterface.so* "$DEST/lib/"
install -m 755 "$ROOT/scripts/setup-permissions.sh" "$DEST/scripts/"
install -m 644 "$ROOT/resources/icons/jot.png" "$HOME/.local/share/icons/hicolor/256x256/apps/jot-linux.png"
ln -sf "$BIN" "$HOME/.local/bin/jot-linux"

cat > "$HOME/.local/share/applications/jot-linux.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Jot
GenericName=Dictation
Comment=Hold a key. Speak. It types.
Exec=$BIN
Icon=jot-linux
Categories=Utility;Accessibility;
Keywords=dictation;speech;voice;transcribe;gemini;
StartupNotify=false
X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input
MimeType=x-scheme-handler/jot;
DESKTOP
# jot:// URLs (settings, history, start-hands-free…)
sed -i "s|^Exec=.*|Exec=$BIN %u|" "$HOME/.local/share/applications/jot-linux.desktop"
xdg-mime default jot-linux.desktop x-scheme-handler/jot 2>/dev/null || true
command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true

# Keep an existing autostart entry pointing at the installed binary.
AUTO="$HOME/.config/autostart/jot-linux.desktop"
if [[ -f "$AUTO" ]]; then sed -i "s|^Exec=.*|Exec=$BIN|" "$AUTO"; fi

echo "Installed to $DEST"
echo "Start it from the app menu (Jot) or run: jot-linux"
