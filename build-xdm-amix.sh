#!/bin/sh
# Build in a fresh, persistent directory; never install or enable XDM here.
set -e
PATH=/usr/public/bin:/usr/ccs/bin:/usr/bin:/bin
export PATH
REPO=${1:-`dirname "$0"`}
REPO=`cd "$REPO" && pwd`
BUILD=${2:-/root/xdm-xrtg-build-`date '+%Y%m%d-%H%M%S'`-$$}
case "$BUILD" in
/*) ;;
*) echo "ERROR: Build path must be absolute." >&2; exit 1 ;;
esac
mkdir "$BUILD"
cp -r /usr/x11r5/clients/xdm/. "$BUILD/"
cd "$BUILD"
patch -p0 < "$REPO/patches/xdm-xrtg-setup.patch"
patch -p0 < "$REPO/patches/xdm-xrtg-auth.patch"
patch -p0 < "$REPO/patches/xdm-session-hints.patch"
patch -p0 < "$REPO/patches/xdm-text-console.patch"
# Force recompilation even when the source tree contains old objects.
rm -f ./*.o ./xdm
OBJECTS='auth.o daemon.o server.o dpylist.o dm.o error.o file.o greet.o netaddr.o reset.o resource.o protodpy.o policy.o session.o socket.o streams.o util.o verify.o xdmcp.o Login.o mitauth.o genauth.o access.o choose.o rpcauth.o xdmsac.o'
make TOP=/usr/x11r5 CC='gcc -ansi -finline-functions -DFamilyNetname=254' PREPROCESSCMD='gcc -ansi -finline-functions -DFamilyNetname=254 -E' $OBJECTS
/usr/ccs/bin/cc -o xdm -O $OBJECTS \
    /usr/X/lib/libXaw.a /usr/X/lib/libXmu.a /usr/X/lib/libXt.a \
    /usr/X/lib/libXext.a /usr/X/lib/libX11.a /usr/X/lib/libXau.a \
    /usr/X/lib/libXdmcp.a /usr/ccs/lib/libm.a -lrpcsvc -lsocket -lnsl \
    /usr/public/lib/gcc-gnulib
echo "Built $BUILD/xdm (not installed)."
