#!/bin/bash
# Install matrix-lock for the current user.
# Everything goes under $HOME; existing files are backed up first.
# Building swaylock-plugin, windowtolayer and neo, and the PAM file, are NOT
# done here - see the README for those.
set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
BACKUP="$HOME/.local/state/matrix-lock-backup-$(date +%Y%m%d-%H%M%S)"
AUTOSTART="$HOME/.config/labwc/autostart"
LINE='swayidle -w >/dev/null 2>&1 &'
RCXML="$HOME/.config/labwc/rc.xml"
XDG_SHELL=/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
IDLE_NOTIFY=/usr/share/wayland-protocols/staging/ext-idle-notify/ext-idle-notify-v1.xml

backup() {  # backup <path>
    if [ -e "$1" ]; then
        mkdir -p "$BACKUP$(dirname "$1")"
        cp -a "$1" "$BACKUP$1"
    fi
}

put() {  # put <src> <dest>
    backup "$2"
    mkdir -p "$(dirname "$2")"
    cp -a "$1" "$2"
}

echo "==> Checking the programs it calls"
missing=0
for b in /usr/local/bin/swaylock-plugin "$HOME/.local/bin/windowtolayer" "$HOME/.local/bin/neo" \
         foot swayidle wlopm notify-send wayland-scanner cc; do
    if ! command -v "$b" >/dev/null; then echo "    missing: $b"; missing=1; fi
done
[ -f /etc/pam.d/swaylock-plugin ] || { echo "    missing: /etc/pam.d/swaylock-plugin"; missing=1; }
for x in "$XDG_SHELL" "$IDLE_NOTIFY"; do
    [ -f "$x" ] || { echo "    missing: $x (wayland-protocols)"; missing=1; }
done
if [ "$missing" = 1 ]; then
    echo "Build and install the missing pieces first (README, Build)." >&2
    exit 1
fi

echo "==> Scripts"
put "$REPO/bin/matrix-lock" "$HOME/.local/bin/matrix-lock"
put "$REPO/bin/lock-test" "$HOME/.local/bin/lock-test"

echo "==> Corner helper and idle-watch (build)"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT
wayland-scanner client-header "$REPO/src/wlr-layer-shell-unstable-v1.xml" "$BUILD/wlr-layer-shell-unstable-v1-client-protocol.h"
wayland-scanner private-code "$REPO/src/wlr-layer-shell-unstable-v1.xml" "$BUILD/layer-shell.c"
wayland-scanner private-code "$XDG_SHELL" "$BUILD/xdg-shell.c"
cc -O2 -I"$BUILD" -o "$BUILD/matrix-corner" "$REPO/src/matrix-corner.c" "$BUILD/layer-shell.c" "$BUILD/xdg-shell.c" -lwayland-client
put "$BUILD/matrix-corner" "$HOME/.local/bin/matrix-corner"
wayland-scanner client-header "$IDLE_NOTIFY" "$BUILD/ext-idle-notify-v1-client-protocol.h"
wayland-scanner private-code "$IDLE_NOTIFY" "$BUILD/ext-idle-notify.c"
cc -O2 -I"$BUILD" -o "$BUILD/idle-watch" "$REPO/src/idle-watch.c" "$BUILD/ext-idle-notify.c" -lwayland-client
put "$BUILD/idle-watch" "$HOME/.local/bin/idle-watch"

echo "==> Config"
put "$REPO/config/swayidle/config" "$HOME/.config/swayidle/config"
put "$REPO/config/matrix-lock/foot.ini" "$HOME/.config/matrix-lock/foot.ini"

echo "==> labwc autostart"
if [ -f "$AUTOSTART" ] && grep -qxF "$LINE" "$AUTOSTART"; then
    echo "    already there"
else
    backup "$AUTOSTART"
    mkdir -p "$(dirname "$AUTOSTART")"
    printf '\n# Idle lock: matrix rain at 5:00 (60 s grace), monitor off at 25:00; config in ~/.config/swayidle/config\n%s\n' \
        "$LINE" >> "$AUTOSTART"
fi

echo "==> labwc keybind: Right Ctrl arms the bottom-right corner"
if [ -f "$RCXML" ] && grep -q 'matrix-corner' "$RCXML"; then
    echo "    already there"
elif [ -f "$RCXML" ] && grep -q '</keyboard>' "$RCXML"; then
    backup "$RCXML"
    # Insert before the first </keyboard>. Never create rc.xml from scratch:
    # one keybind of ours there would replace all of labwc's default keybinds.
    awk -v cmd="$HOME/.local/bin/matrix-corner" '
        !done && /<\/keyboard>/ {
            print "    <!-- matrix-lock: Right Ctrl arms the bottom-right corner for 2 s; touching it locks -->"
            print "    <keybind key=\"Control_R\">"
            print "      <action name=\"Execute\" command=\"" cmd "\" />"
            print "    </keybind>"
            done = 1
        }
        { print }' "$RCXML" > "$RCXML.new" && mv "$RCXML.new" "$RCXML"
    [ -n "${WAYLAND_DISPLAY:-}" ] && labwc --reconfigure 2>/dev/null || true
else
    echo "    no <keyboard> section in $RCXML; add the keybind by hand (README, Corner gesture)"
fi

echo "==> labwc keybind: Win+L locks with the rain"
LOCKCMD="$HOME/.local/bin/matrix-lock"
if [ ! -f "$RCXML" ] || ! grep -q '</keyboard>' "$RCXML"; then
    echo "    no <keyboard> section in $RCXML; add the keybind by hand (README, Lock now)"
elif grep -qF "command=\"$LOCKCMD\"" "$RCXML"; then
    echo "    already there"
else
    backup "$RCXML"
    # Repoint an existing W-l bind (LXQt ships it as lxqt-leave --lockscreen,
    # which locks nothing under labwc); add one if there is none.
    awk -v cmd="$LOCKCMD" '
        /<keybind key="W-l">/ { inwl = 1; found = 1 }
        inwl && /command="/ { sub(/command="[^"]*"/, "command=\"" cmd "\"") }
        inwl && /<\/keybind>/ { inwl = 0 }
        !found && !added && /<\/keyboard>/ {
            print "    <keybind key=\"W-l\">"
            print "      <action name=\"Execute\" command=\"" cmd "\" />"
            print "    </keybind>"
            added = 1
        }
        { print }' "$RCXML" > "$RCXML.new" && mv "$RCXML.new" "$RCXML"
    [ -n "${WAYLAND_DISPLAY:-}" ] && labwc --reconfigure 2>/dev/null || true
fi

[ -d "$BACKUP" ] && echo "Backups in $BACKUP"
echo "Done. Test with 'lock-test matrix 90' before logging out and in again (README, Test safely)."
