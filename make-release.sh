#!/bin/sh
# make-release.sh — Package Xrtg into a binary release tar archive
#
# Run on AMIX after a successful build.
#
# Usage:
#   sh /path/to/make-release.sh [binary] [version]
#
#   binary:  path to compiled Xrtg (default: /usr/x11r5/server/Xrtg)
#   version: release tag (default: YYYYMMDD from date)
#
# Output: /tmp/xrtg-<version>.tar
#
# Install on target machine (no network required):
#   tar xf xrtg-<version>.tar
#   sh xrtg-<version>/install-xrtg.sh xrtg-<version>/Xrtg

set -e

BINARY="${1:-/usr/x11r5/server/Xrtg}"
VERSION="${2:-`date '+%Y%m%d'`}"
PKGNAME="xrtg-$VERSION"
STAGEDIR="/tmp/$PKGNAME"
TARFILE="/tmp/$PKGNAME.tar"
SCRIPT_DIR=`dirname "$0"`

# --------------------------------------------------------------------------
# Sanity checks
# --------------------------------------------------------------------------

if [ ! -f "$BINARY" ]; then
    echo "ERROR: Binary not found: $BINARY" >&2
    echo "Build it first: cd /usr/x11r5/server && make Xrtg" >&2
    exit 1
fi

if [ ! -f "$SCRIPT_DIR/install-xrtg.sh" ]; then
    echo "ERROR: install-xrtg.sh not found next to this script." >&2
    echo "       Run from the xrtg-amix repo directory." >&2
    exit 1
fi

# --------------------------------------------------------------------------
# Stage files
# --------------------------------------------------------------------------

echo "Staging in $STAGEDIR ..."

rm -rf "$STAGEDIR"
mkdir "$STAGEDIR"

cp "$BINARY"                      "$STAGEDIR/Xrtg"
chmod 755                         "$STAGEDIR/Xrtg"
cp "$SCRIPT_DIR/install-xrtg.sh" "$STAGEDIR/install-xrtg.sh"
cp "$SCRIPT_DIR/start_xrtg.sh"   "$STAGEDIR/start_xrtg.sh"

cat > "$STAGEDIR/INSTALL.txt" <<EOF
Xrtg binary release $VERSION — X11R5 RTG server for AMIX / MNT VA2000
=======================================================================

Requirements
  - Amiga UNIX (AMIX) SVR4 2.1p2a
  - MNT VA2000 graphics card and kernel driver (/dev/va2000 must exist)
    Driver source: https://github.com/asokero/va2000-amix
  - No X server currently running

Install (run as root)
  tar xf xrtg-$VERSION.tar
  sh xrtg-$VERSION/install-xrtg.sh xrtg-$VERSION/Xrtg

  install-xrtg.sh does the following:
    - Copies Xrtg to /usr/bin/X11/Xrtg (backs up any existing binary)
    - Creates /usr/bin/X11/X -> Xrtg symlink
    - Creates /usr/X/lib/fonts/Speedo/fonts.dir (required by X font path)

Start X (from a root shell or telnet session)
  sh /path/to/start_xrtg.sh

  Starts Xrtg, then twm, xclock, and two xterm windows.
  All clients use nohup so they survive when the shell session closes.

If input (keyboard/mouse) stops working
  Power the Amiga completely off, wait 10 seconds, power on.
  Do not use kill -9 on Xrtg if you can avoid it — send SIGTERM first.

Revert to previous X server
  cp /usr/bin/X11/Xrtg.orig /usr/bin/X11/Xrtg
  rm /usr/bin/X11/X
  ln -s Xdmi /usr/bin/X11/X    (or whatever server was in use before)
EOF

# --------------------------------------------------------------------------
# Create tar archive
# --------------------------------------------------------------------------

echo "Creating $TARFILE ..."

cd /tmp
tar cf "$TARFILE" "$PKGNAME"

rm -rf "$STAGEDIR"

echo ""
ls -l "$TARFILE"
echo ""
echo "Done. To install on a target AMIX machine:"
echo "  tar xf $PKGNAME.tar"
echo "  sh $PKGNAME/install-xrtg.sh $PKGNAME/Xrtg"
