#!/bin/sh
# Install a patched AMIX binary and optionally a newly provisioned seed.
# Usage: sh install-xdm-binary.sh /absolute/build/xdm [/absolute/private/seed]
set -e
umask 077
BIN=$1
SEED=${2:-/usr/X/lib/xdm/Xrtg-seed}
DIR=/usr/X/lib/xdm
if [ "`whoami`" != root ] || [ ! -f "$BIN" ]; then
    echo "ERROR: Run as root with a compiled XDM binary." >&2; exit 1
fi
if ps -e | awk '$NF == "xdm" { found=1 } END { exit !found }'; then
    echo "ERROR: Stop XDM before installing its binary." >&2; exit 1
fi
strings -a "$BIN" | grep XRTG_SECURE_COOKIE_V1 >/dev/null
if [ ! -f "$SEED" ] || [ "`wc -c < "$SEED"`" -ne 32 ]; then
    echo "ERROR: Supply a private 32-byte random seed." >&2; exit 1
fi
BACKUP=/root/xdm-binary-backup-`date '+%Y%m%d-%H%M%S'`-$$
mkdir "$BACKUP"
cp -p /usr/X/bin/xdm "$BACKUP/xdm"
ls -ld "$DIR" > "$BACKUP/xdm-directory-metadata"
chown root "$DIR"
chmod 755 "$DIR"
# Do not back up or restore old seed state: it must keep advancing.
cp "$SEED" "$DIR/Xrtg-seed.install"
chown root "$DIR/Xrtg-seed.install"
chmod 600 "$DIR/Xrtg-seed.install"
mv "$DIR/Xrtg-seed.install" "$DIR/Xrtg-seed"
cp "$BIN" /usr/X/bin/xdm.new
chown root /usr/X/bin/xdm.new
chmod 755 /usr/X/bin/xdm.new
mv /usr/X/bin/xdm.new /usr/X/bin/xdm
echo "Installed. Old binary: $BACKUP/xdm"
echo "Boot mode unchanged."
