#!/bin/sh
# install-xdm.sh - stage the Xrtg xdm configuration on AMIX.
#
# Usage:
#   sh install-xdm.sh [repo-root] [local-backdrop]
#
# Example:
#   sh install-xdm.sh /path/to/xrtg-amix /path/to/local-backdrop.gif
#
# This script never enables xdm at boot.  Activation is deliberately separate
# and is handled by /usr/X/bin/xdm-mode after visual and security checks.

set -e
umask 077

REPO=${1:-`dirname "$0"`}
BACKDROP=${2:-}
XDMSRC="$REPO/session/xdm"
SESSIONSRC="$REPO/session"
XDMDIR=/usr/X/lib/xdm
SESSIONDIR=/usr/X/lib/xrtg
MODEDEST=/usr/X/bin/xdm-mode
SACTAB=/etc/saf/_sactab

if [ "`whoami`" != "root" ]; then
    echo "ERROR: This script must be run as root." >&2
    exit 1
fi

for file in xdm-config Xservers Xresources Xsetup_0 Xaccess-local
do
    if [ ! -f "$XDMSRC/$file" ]; then
        echo "ERROR: Missing $XDMSRC/$file" >&2
        exit 1
    fi
done

for file in Xdefaults twmrc session.conf xrtg-session.sh
do
    if [ ! -f "$SESSIONSRC/$file" ]; then
        echo "ERROR: Missing $SESSIONSRC/$file" >&2
        exit 1
    fi
done

if [ ! -f "$REPO/xdm-mode.sh" ]; then
    echo "ERROR: Missing $REPO/xdm-mode.sh" >&2
    exit 1
fi

if [ ! -f "$REPO/xdm-console.sh" ]; then
    echo "ERROR: Missing $REPO/xdm-console.sh" >&2
    exit 1
fi

if [ ! -x /usr/X/bin/xdm ] || [ ! -x /usr/bin/X11/Xrtg ]; then
    echo "ERROR: xdm or Xrtg is not installed." >&2
    exit 1
fi

if [ -n "$BACKDROP" ]; then
    case "$BACKDROP" in
    *"'"*) echo "ERROR: Backdrop path cannot contain a single quote." >&2; exit 1 ;;
    esac
    case "$BACKDROP" in
    /*) ;;
    *)
        echo "ERROR: Backdrop path must be absolute: $BACKDROP" >&2
        exit 1
        ;;
    esac
    if [ ! -f "$BACKDROP" ]; then
        echo "ERROR: Backdrop does not exist: $BACKDROP" >&2
        exit 1
    fi
fi

# Do not replace files underneath a running display manager. AMIX pads its
# pid file with spaces; use the process table, including when that file is stale.
if ps -e | awk '$NF == "xdm" { found=1 } END { exit !found }'; then
    echo "ERROR: xdm is running. Stop it with: sacadm -k -p xdm" >&2
    exit 1
fi

STAMP=`date '+%Y%m%d-%H%M%S'`
BACKUP="/root/xdm-production-backup-$STAMP"
mkdir "$BACKUP"
mkdir "$BACKUP/xdm"
mkdir "$BACKUP/xrtg"
mkdir "$BACKUP/bin"

cp "$SACTAB" "$BACKUP/_sactab"
ls -ld "$XDMDIR" > "$BACKUP/xdm-directory-metadata"
# The seed and authority files must live in a directory writable only by root.
chown root "$XDMDIR"
chmod 755 "$XDMDIR"

ABSENT="$BACKUP/absent-before-install"
echo "Files listed here did not exist before this installation:" > "$ABSENT"

for file in xdm-config Xservers Xresources Xsetup_0 Xaccess-local
do
    if [ -f "$XDMDIR/$file" ]; then
        cp "$XDMDIR/$file" "$BACKUP/xdm/$file"
    else
        echo "$XDMDIR/$file" >> "$ABSENT"
    fi
done

for file in Xdefaults twmrc session.conf session.conf.local xrtg-session
do
    if [ -f "$SESSIONDIR/$file" ]; then
        cp "$SESSIONDIR/$file" "$BACKUP/xrtg/$file"
    else
        echo "$SESSIONDIR/$file" >> "$ABSENT"
    fi
done

if [ -f "$MODEDEST" ]; then
    cp "$MODEDEST" "$BACKUP/bin/xdm-mode"
else
    echo "$MODEDEST" >> "$ABSENT"
fi

if [ ! -d "$SESSIONDIR" ]; then
    mkdir "$SESSIONDIR"
fi

if [ -f /usr/X/bin/xdm-console ]; then
    cp -p /usr/X/bin/xdm-console "$BACKUP/bin/xdm-console"
else
    echo /usr/X/bin/xdm-console >> "$ABSENT"
fi
chown root "$SESSIONDIR"
chmod 755 "$SESSIONDIR"

cp "$SESSIONSRC/Xdefaults" "$SESSIONDIR/Xdefaults"
cp "$SESSIONSRC/twmrc" "$SESSIONDIR/twmrc"
cp "$SESSIONSRC/session.conf" "$SESSIONDIR/session.conf"
cp "$SESSIONSRC/xrtg-session.sh" "$SESSIONDIR/xrtg-session"

chmod 644 "$SESSIONDIR/Xdefaults" "$SESSIONDIR/twmrc" "$SESSIONDIR/session.conf"
chmod 755 "$SESSIONDIR/xrtg-session"

if [ -n "$BACKDROP" ]; then
    echo "XRTG_BACKDROP='$BACKDROP'" > "$SESSIONDIR/session.conf.local"
chmod 600 "$SESSIONDIR/session.conf.local"
    echo "Configured local backdrop: $BACKDROP"
fi

for file in xdm-config Xservers Xresources Xaccess-local
do
    cp "$XDMSRC/$file" "$XDMDIR/$file"
    chmod 644 "$XDMDIR/$file"
done
cp "$XDMSRC/Xsetup_0" "$XDMDIR/Xsetup_0"
chmod 755 "$XDMDIR/Xsetup_0"

cp "$REPO/xdm-mode.sh" "$MODEDEST"
cp "$REPO/xdm-console.sh" /usr/X/bin/xdm-console
chown root /usr/X/bin/xdm-console
chmod 700 /usr/X/bin/xdm-console
chmod 755 "$MODEDEST"

echo ""
echo "Xrtg xdm files installed, but xdm was NOT enabled."
echo "Backup: $BACKUP"
echo ""
echo "Next steps:"
echo "  1. Review $SESSIONDIR/session.conf.local"
echo "  2. Start a non-boot test: sacadm -s -p xdm"
echo "  3. After login/logout and authorization tests: $MODEDEST enable"
