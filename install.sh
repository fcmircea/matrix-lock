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
         foot swayidle wlopm notify-send; do
    if ! command -v "$b" >/dev/null; then echo "    missing: $b"; missing=1; fi
done
[ -f /etc/pam.d/swaylock-plugin ] || { echo "    missing: /etc/pam.d/swaylock-plugin"; missing=1; }
if [ "$missing" = 1 ]; then
    echo "Build and install the missing pieces first (README, Build)." >&2
    exit 1
fi

echo "==> Scripts"
put "$REPO/bin/matrix-lock" "$HOME/.local/bin/matrix-lock"
put "$REPO/bin/lock-test" "$HOME/.local/bin/lock-test"

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

[ -d "$BACKUP" ] && echo "Backups in $BACKUP"
echo "Done. Test with 'lock-test matrix 90' before logging out and in again (README, Test safely)."
