#!/bin/sh
# install-xrtg.sh — Install the compiled Xrtg binary and set up the X symlink
#
# Usage: run from /usr/x11r5/server (where make Xrtg placed the binary)
#   sh /path/to/install-xrtg.sh
#
# Optional: pass the binary path explicitly
#   sh /path/to/install-xrtg.sh /usr/x11r5/server/Xrtg
#
# Must be run as root. Backs up any existing Xrtg before overwriting.

set -e

BINDIR=/usr/bin/X11
BINARY="${1:-./Xrtg}"
DEST="$BINDIR/Xrtg"

# --------------------------------------------------------------------------
# Sanity checks
# --------------------------------------------------------------------------

if [ "`whoami`" != "root" ]; then
    echo "ERROR: This script must be run as root." >&2
    exit 1
fi

if [ ! -f "$BINARY" ]; then
    echo "ERROR: Binary not found: $BINARY" >&2
    echo "" >&2
    echo "Build it first:" >&2
    echo "  cd /usr/x11r5/fonts/lib/font && make" >&2
    echo "  cd /usr/x11r5/extensions/server && make" >&2
    echo "  cd /usr/x11r5/server && make Makefiles && make depend && make Xrtg" >&2
    exit 1
fi

if [ ! -d "$BINDIR" ]; then
    echo "ERROR: Target directory does not exist: $BINDIR" >&2
    exit 1
fi

# --------------------------------------------------------------------------
# Warn if an X server is running
# --------------------------------------------------------------------------

if [ -f /tmp/.X0-lock ]; then
    echo "WARNING: X server appears to be running (found /tmp/.X0-lock)."
    echo "         Stop the X server before installing."
    echo "         Proceeding anyway in 5 seconds — press Ctrl-C to abort."
    sleep 5
fi

# --------------------------------------------------------------------------
# Install binary
# --------------------------------------------------------------------------

echo "Installing binary..."

if [ -f "$DEST" ]; then
    cp "$DEST" "$DEST.orig"
    echo "  backup:   $DEST.orig"
fi

cp "$BINARY" "$DEST"
chmod 755 "$DEST"
echo "  installed: $DEST"

# --------------------------------------------------------------------------
# Fix font path
#
# The compiled-in default font path includes Speedo/ which does not exist
# on this AMIX installation.  X11R5 rejects the entire font path if any
# component directory is missing, so even the 'fixed' font in misc/ becomes
# unreachable.
#
# /usr/lib/X11 -> /usr/X/lib already exists (vanilla symlink), so
# /usr/lib/X11/fonts/misc/ resolves correctly.  We just need to create
# an empty Speedo/ directory with a valid fonts.dir so the path check passes.
# --------------------------------------------------------------------------

echo "Fixing font path..."

FONTBASE=/usr/X/lib/fonts
SPEEDODIR="$FONTBASE/Speedo"

if [ ! -d "$SPEEDODIR" ]; then
    mkdir "$SPEEDODIR"
    echo "0" > "$SPEEDODIR/fonts.dir"
    echo "  created:  $SPEEDODIR/fonts.dir (empty — satisfies font path check)"
elif [ ! -f "$SPEEDODIR/fonts.dir" ]; then
    echo "0" > "$SPEEDODIR/fonts.dir"
    echo "  created:  $SPEEDODIR/fonts.dir (was missing)"
else
    echo "  ok:       $SPEEDODIR/fonts.dir"
fi

echo ""

# --------------------------------------------------------------------------
# Set up X symlink
# --------------------------------------------------------------------------

echo "Setting up X symlink..."

XLINK="$BINDIR/X"

if [ -h "$XLINK" ]; then
    OLD=`ls -la "$XLINK" | awk '{print $NF}'`
    rm "$XLINK"
    echo "  removed old symlink: X -> $OLD"
elif [ -f "$XLINK" ]; then
    cp "$XLINK" "$XLINK.orig"
    rm "$XLINK"
    echo "  backed up old X binary: $XLINK.orig"
fi

ln -s Xrtg "$XLINK"
echo "  created:  $XLINK -> Xrtg"

# --------------------------------------------------------------------------
# Install start script
# --------------------------------------------------------------------------

echo "Installing start script..."

SCRIPT_DIR=`dirname "$0"`
STARTSCRIPT="$SCRIPT_DIR/start_xrtg.sh"
STARTDEST="/usr/X/bin/startxrtg"

if [ -f "$STARTSCRIPT" ]; then
    cp "$STARTSCRIPT" "$STARTDEST"
    chmod 755 "$STARTDEST"
    echo "  installed: $STARTDEST"
else
    echo "  skipped:   start_xrtg.sh not found next to install-xrtg.sh"
fi

# --------------------------------------------------------------------------
# Done
# --------------------------------------------------------------------------

echo ""
echo "=== Xrtg installed ==="
echo ""
echo "To start the server (from telnet or a root console shell):"
echo "  startxrtg"
echo ""
echo "  startxrtg starts Xrtg, then launches twm, xclock, and two xterms."
echo "  All clients use nohup so they survive when the calling shell session closes."
echo ""
echo "To verify it is running:"
echo "  ps -ef | grep Xrtg"
echo "  xdpyinfo -display :0"
echo ""
echo "If the server fails with 'could not open default font fixed':"
echo "  Check /usr/X/lib/fonts/Speedo/fonts.dir exists (this script creates it)."
echo ""
echo "If the server fails to start, check:"
echo "  - /dev/va2000 exists and is readable by root"
echo "  - No other X server is using display :0  (rm /tmp/.X0-lock if stale)"
echo "  - Kernel RTG driver is loaded"
echo ""
echo "To revert to the previous server:"
if [ -f "$DEST.orig" ]; then
    echo "  cp $DEST.orig $DEST"
fi
echo "  rm $XLINK"
echo "  ln -s Xdmi $XLINK    (or whatever the previous server was)"
