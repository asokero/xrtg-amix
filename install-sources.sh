#!/bin/sh
# install-sources.sh — Copy Xrtg sources into a vanilla X11R5 installation
#
# Usage: run from /usr/x11r5
#   sh /path/to/install-sources.sh /path/to/changes
#
# $1: directory containing the modified tree (config/ server/ etc.)
# If omitted, the script's own directory is used as the source.
#
# Creates .orig backups of modified files before overwriting.

set -e

# --------------------------------------------------------------------------
# Sanity checks
# --------------------------------------------------------------------------

if [ ! -f config/amix.cf ] || [ ! -d server/ddx/amix ]; then
    echo "ERROR: Run this script from /usr/x11r5." >&2
    echo "       Current directory: `pwd`" >&2
    exit 1
fi

SCRIPT_DIR=`dirname "$0"`
SRCDIR="${1:-$SCRIPT_DIR}"

if [ ! -f "$SRCDIR/config/amix.cf" ]; then
    echo "ERROR: Source files not found in: $SRCDIR" >&2
    echo "       Expected: $SRCDIR/config/amix.cf" >&2
    echo "       Pass the changes directory as argument: sh install-sources.sh /path" >&2
    exit 1
fi

echo "Source directory: $SRCDIR"
echo "Target directory: `pwd`"
echo ""

# --------------------------------------------------------------------------
# Install a modified file: back up original, then overwrite
# --------------------------------------------------------------------------

install_modified() {
    SRC="$SRCDIR/$1"
    DST="$1"
    if [ ! -f "$SRC" ]; then
        echo "ERROR: Source file not found: $SRC" >&2
        exit 1
    fi
    if [ ! -f "$DST.orig" ]; then
        cp "$DST" "$DST.orig"
        echo "  backup:   $DST.orig"
    else
        echo "  backup already exists: $DST.orig  (not overwritten)"
    fi
    cp "$SRC" "$DST"
    echo "  updated:  $DST"
}

# --------------------------------------------------------------------------
# Install a new file: no backup needed
# --------------------------------------------------------------------------

install_new() {
    SRC="$SRCDIR/$1"
    DST="$1"
    if [ ! -f "$SRC" ]; then
        echo "ERROR: Source file not found: $SRC" >&2
        exit 1
    fi
    cp "$SRC" "$DST"
    echo "  new:      $DST"
}

# --------------------------------------------------------------------------
# Install a pre-built binary from an absolute path on the host system.
# Used for libraries that exist pre-compiled elsewhere on AMIX but are
# expected at a different path by the build system.
# --------------------------------------------------------------------------

install_prebuilt() {
    SRC="$1"
    DST="$2"
    if [ ! -f "$SRC" ]; then
        echo "ERROR: Pre-built source not found: $SRC" >&2
        exit 1
    fi
    cp "$SRC" "$DST"
    echo "  prebuilt: $DST  (from $SRC)"
}

# --------------------------------------------------------------------------
# 1. Modified files — differ from vanilla
# --------------------------------------------------------------------------

echo "=== Modified files ==="

install_modified config/amix.cf
install_modified server/Imakefile
install_modified server/ddx/amix/Imakefile
install_modified server/ddx/amix/amixInit.c
install_modified server/ddx/amix/amixCursor.c

echo ""

# --------------------------------------------------------------------------
# 2. New files — imake config rules (missing from vanilla installation)
# --------------------------------------------------------------------------

echo "=== New files: config/ ==="

install_new config/Imake.rules
install_new config/noop.rules
install_new config/sv4Lib.rules
install_new config/amix.rules

echo ""

# --------------------------------------------------------------------------
# 3. makedepend binary
#
# makedepend sources include <X11/Xosdefs.h> which is not in a path the
# compiler finds during the util/makedepend build.  The vanilla installation
# ships a pre-compiled binary at /usr/X/bin/makedepend.  The build system
# expects makedepend at $(UTILSRC)/makedepend/makedepend, so we install it
# there directly — no compilation needed.
# --------------------------------------------------------------------------

echo "=== makedepend binary ==="

install_new util/makedepend/makedepend
chmod 755 util/makedepend/makedepend

echo ""

# --------------------------------------------------------------------------
# 4. Header symlinks
#
# imake sets INCLUDESRC = $(TOP)/X11 = /usr/x11r5/X11 in every generated
# Makefile.  Server sources use #include "X.h" style so makedepend is
# invoked with -I$(INCLUDESRC) = -I/usr/x11r5/X11.  On AMIX the headers
# live in /usr/x11r5/include/ without an X11/ subdirectory, so makedepend
# cannot find X.h and writes bare "X.h" into .depend files.  make then
# tries to build X.h as a target and fails with "don't know how to make".
#
# Fix A: symlink /usr/x11r5/X11 -> include so INCLUDESRC resolves correctly.
#
# Fix B: os/xdmcp.c and os/xdmauth.c include "Xdmcp.h" which lives in
# lib/Xdmcp/Xdmcp.h.  The os/ Imakefile INCLUDES does not add -I$(TOP)/lib/Xdmcp,
# so makedepend cannot find it and writes bare "Xdmcp.h" into .depend files.
# Fix: symlink include/Xdmcp.h -> ../lib/Xdmcp/Xdmcp.h so -I$(INCLUDESRC)
# (= -I/usr/x11r5/include) resolves it.
# --------------------------------------------------------------------------

echo "=== Header symlinks ==="

if [ -h X11 ]; then
    echo "  exists:   X11 -> `ls -la X11 | awk '{print $NF}'`"
elif [ -d X11 ]; then
    echo "  skipped:  X11 is already a real directory"
else
    ln -s include X11
    echo "  created:  X11 -> include"
fi

if [ -h include/Xdmcp.h ]; then
    echo "  exists:   include/Xdmcp.h -> `ls -la include/Xdmcp.h | awk '{print $NF}'`"
elif [ -f include/Xdmcp.h ]; then
    echo "  skipped:  include/Xdmcp.h already exists as a real file"
else
    ln -s ../lib/Xdmcp/Xdmcp.h include/Xdmcp.h
    echo "  created:  include/Xdmcp.h -> ../lib/Xdmcp/Xdmcp.h"
fi

echo ""

# --------------------------------------------------------------------------
# 5. New directories
# --------------------------------------------------------------------------

echo "=== New directories ==="

for DIR in \
    server/ddx/amix/rtg \
    server/ddx/amix/rtg/va2000
do
    if [ ! -d "$DIR" ]; then
        mkdir -p "$DIR"
        echo "  created:  $DIR"
    else
        echo "  exists:   $DIR"
    fi
done

echo ""

# --------------------------------------------------------------------------
# 6. New files — RTG core layer
# --------------------------------------------------------------------------

echo "=== New files: rtg/ ==="

install_new server/ddx/amix/rtg/Imakefile
install_new server/ddx/amix/rtg/rtg.h
install_new server/ddx/amix/rtg/rtgInit.c

echo ""

# --------------------------------------------------------------------------
# 7. New files — VA2000 driver
# --------------------------------------------------------------------------

echo "=== New files: rtg/va2000/ ==="

install_new server/ddx/amix/rtg/va2000/Imakefile
install_new server/ddx/amix/rtg/va2000/va2000.h
install_new server/ddx/amix/rtg/va2000/va2000hw.c
install_new server/ddx/amix/rtg/va2000/va2000screen.c
install_new server/ddx/amix/rtg/va2000/va2000draw.c
install_new server/ddx/amix/rtg/va2000/va2000win.c
install_new server/ddx/amix/rtg/va2000/va2000cmap.c
install_new server/ddx/amix/rtg/va2000/va2000pix.c
install_new server/ddx/amix/rtg/va2000/va2000cursor.c

echo ""

# --------------------------------------------------------------------------
# 8. Pre-built libraries
#
# The server link needs libXau.a and libXdmcp.a at the paths imake defines:
#   XAUTHSRC  = $(TOP)/lib/Xau   → /usr/x11r5/lib/Xau/libXau.a
#   XDMCPLIBSRC = $(TOP)/lib/Xdmcp → /usr/x11r5/lib/Xdmcp/libXdmcp.a
#
# These directories exist (source files are there) but no pre-built .a.
# The running X installation keeps pre-compiled m68k versions in /usr/X/lib/;
# libXau and libXdmcp are identical client/server libraries so those binaries
# are compatible.  Copy them directly — no source build needed.
#
# libfont.a (fonts/lib/font/) and extensions/server/libext.a must be built
# from source before the server link step — see sections 9 and the build
# instructions below.  extensions/server/Makefile is regenerated in section 10
# so that it uses our amix.cf settings (BuildPexExt NO) instead of the vanilla
# Makefile which was generated with PEX enabled and fails on missing PEXproto.h.
# --------------------------------------------------------------------------

echo "=== Pre-built libraries ==="

for DIR in lib/Xau lib/Xdmcp; do
    if [ ! -d "$DIR" ]; then
        mkdir -p "$DIR"
        echo "  created:  $DIR"
    fi
done

install_prebuilt /usr/X/lib/libXau.a   lib/Xau/libXau.a
install_prebuilt /usr/X/lib/libXdmcp.a lib/Xdmcp/libXdmcp.a

echo ""

# --------------------------------------------------------------------------
# 9. Pre-build checks
#
# libfont.a and libext.a must exist before "make Xrtg" runs.  They are not
# pre-installed anywhere — they must be compiled from source.  Warn here if
# either is missing so the user knows what to do next.
# --------------------------------------------------------------------------

echo "=== Pre-build checks ==="

MISSING_LIBS=0

if [ ! -f fonts/lib/font/libfont.a ]; then
    echo ""
    echo "  NOTE: fonts/lib/font/libfont.a not found — must be built before make Xrtg"
    echo "    cd /usr/x11r5/fonts/lib/font && make"
    MISSING_LIBS=1
else
    echo "  ok:  fonts/lib/font/libfont.a"
fi

if [ ! -f extensions/server/libext.a ]; then
    echo ""
    echo "  NOTE: extensions/server/libext.a not found — must be built before make Xrtg"
    echo "    cd /usr/x11r5/extensions/server && make"
    MISSING_LIBS=1
else
    echo "  ok:  extensions/server/libext.a"
fi

echo ""

# --------------------------------------------------------------------------
# Done
# --------------------------------------------------------------------------

echo "=== Generating Makefiles ==="

# extensions/server/Makefile: the vanilla file was generated with BuildPexExt
# enabled and tries to enter PEX/dipex/swap which needs PEXproto.h.  Regenerate
# with our amix.cf so that BuildPexExt NO gives empty SUBDIRS — only libext.a
# is built from shape.c shm.c multibuf.c mitmisc.c xtest1di.c xtest1dd.c.
cd extensions/server
/usr/x11r5/config/imake -I../../config -DTOPDIR=/usr/x11r5 -DCURDIR=/usr/x11r5/extensions/server
echo "  done: extensions/server/Makefile"
cd ../..

cd server
/usr/x11r5/config/imake -I../config -DTOPDIR=/usr/x11r5 -DCURDIR=/usr/x11r5/server
echo "  done: server/Makefile"
cd ..

echo ""

# --------------------------------------------------------------------------
# 10. Font path fix
#
# The compiled-in default font path includes Speedo/ which does not exist
# on this AMIX installation.  X11R5 rejects the entire font path if any
# component directory is missing, so 'fixed' in misc/ becomes unreachable.
# /usr/lib/X11 -> /usr/X/lib already exists (vanilla symlink).
# Create an empty Speedo/fonts.dir so the path check passes.
# --------------------------------------------------------------------------

echo "=== Font path fix ==="

SPEEDODIR=/usr/X/lib/fonts/Speedo

if [ ! -d "$SPEEDODIR" ]; then
    mkdir "$SPEEDODIR"
    echo "0" > "$SPEEDODIR/fonts.dir"
    echo "  created:  $SPEEDODIR/fonts.dir"
elif [ ! -f "$SPEEDODIR/fonts.dir" ]; then
    echo "0" > "$SPEEDODIR/fonts.dir"
    echo "  created:  $SPEEDODIR/fonts.dir (dir existed, file was missing)"
else
    echo "  ok:       $SPEEDODIR/fonts.dir"
fi

echo ""
echo "=== Sources installed ==="
echo ""
echo "Build commands (run from /usr/x11r5):"
echo ""
if [ "$MISSING_LIBS" = "1" ]; then
echo "  *** Build these first — they must exist before the server link: ***"
echo ""
fi
echo "  cd /usr/x11r5/fonts/lib/font && make"
echo "  cd /usr/x11r5/extensions/server && make"
echo "  cd /usr/x11r5/server && make Makefiles && make depend && make Xrtg"
echo ""
echo "If you need to regenerate server/Makefile manually:"
echo "  cd /usr/x11r5/server"
echo "  /usr/x11r5/config/imake -I../config -DTOPDIR=/usr/x11r5 -DCURDIR=/usr/x11r5/server"
echo ""
echo "To revert if something goes wrong:"
echo "  cp config/amix.cf.orig config/amix.cf"
echo "  cp server/Imakefile.orig server/Imakefile"
echo "  cp server/ddx/amix/Imakefile.orig server/ddx/amix/Imakefile"
echo "  cp server/ddx/amix/amixInit.c.orig server/ddx/amix/amixInit.c"
echo "  cp server/ddx/amix/amixCursor.c.orig server/ddx/amix/amixCursor.c"
echo "  rm -f config/Imake.rules config/noop.rules config/sv4Lib.rules config/amix.rules"
echo "  rm -f util/makedepend/makedepend"
echo "  rm -f X11"
echo "  rm -f include/Xdmcp.h"
echo "  rm -f lib/Xau/libXau.a"
echo "  rm -f lib/Xdmcp/libXdmcp.a"
echo "  rm -rf server/ddx/amix/rtg"
echo "  rm -rf /usr/X/lib/fonts/Speedo"
