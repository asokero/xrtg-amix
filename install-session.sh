#!/bin/sh
# install-session.sh — install the session appearance files.
#
#   sh install-session.sh [repo-root]
#
# Copies session/Xdefaults and session/twmrc to /root/.Xdefaults and
# /root/.twmrc.  Anything already there is kept as a .bak first, because
# these are the files people hand-edit.
#
# start_xrtg.sh loads .Xdefaults with xrdb and twm reads .twmrc, so the
# session picks both up on its next start.  To apply .Xdefaults to a session
# that is already running:
#
#   DISPLAY=:0 xrdb -load /root/.Xdefaults
#
# twm needs a restart: its own menu has "Restart twm".

REPO=${1:-`dirname $0`}

if [ -d "$REPO/session" ]; then
    SRC="$REPO/session"
elif [ -d "$REPO/../session" ]; then
    SRC="$REPO/../session"
else
    echo "install-session.sh: no session/ directory under $REPO" >&2
    exit 1
fi

install_one() {
    src="$SRC/$1"
    dst="$2"
    if [ ! -f "$src" ]; then
        echo "  missing: $src" >&2
        return 1
    fi
    if [ -f "$dst" ]; then
        cp "$dst" "$dst.bak" && echo "  kept old $dst as $dst.bak"
    fi
    cp "$src" "$dst" && echo "  installed $dst"
}

echo "Installing session files from $SRC"
install_one Xdefaults /root/.Xdefaults
install_one twmrc     /root/.twmrc

echo ""
echo "Start a new session with startxrtg, or apply the resources now with:"
echo "  DISPLAY=:0 xrdb -load /root/.Xdefaults"
