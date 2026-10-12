#!/usr/bin/env bash
# One-time: let the logged-in desktop user read keyboard input devices, so Jot can
# see a bare-modifier dictation key (Right Ctrl, …) on Wayland.
#
# Uses systemd-logind's "uaccess" ACL — access follows the active local session
# (not a permanent group membership) and is revoked on logout / user switch.
# Undo: sudo rm /etc/udev/rules.d/70-jot-keyboard-access.rules && sudo udevadm trigger
set -euo pipefail
if [[ $EUID -ne 0 ]]; then
    exec sudo "$0" "$@"
fi
RULE=/etc/udev/rules.d/70-jot-keyboard-access.rules
cat > "$RULE" <<'RULES'
# Jot (dictation): the active seat user may read keyboards to detect the dictation key.
SUBSYSTEM=="input", KERNEL=="event*", ENV{ID_INPUT_KEYBOARD}=="1", TAG+="uaccess"
RULES
chmod 644 "$RULE"
udevadm control --reload-rules
udevadm trigger --subsystem-match=input --action=change
echo "Keyboard access granted to the active desktop session. Press \"Check Again\" in Jot."
