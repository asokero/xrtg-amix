#!/bin/sh
# xdm-mode.sh - safely select xdm or manual startxrtg mode on AMIX.
set -e
PATH=/usr/X/bin:/usr/bin:/usr/sbin:/usr/ccs/bin:/usr/ucb:/bin
export PATH
umask 077

SACTAB=/etc/saf/_sactab
XDMPID=/usr/X/lib/xdm/xdm-pid
TMP=/etc/saf/.xdm-mode.$$

# AMIX sh discards the original exit status on entry to an EXIT trap.
# Successful updates rename TMP; interrupted updates remove it here.
trap 'rm -f "$TMP"; exit 1' 1 2 15

if [ "`whoami`" != "root" ]; then
    echo "ERROR: This script must be run as root." >&2
    exit 1
fi

usage()
{
    echo "Usage: $0 status | enable | disable" >&2
    exit 1
}

xdm_running()
{
    if [ -s "$XDMPID" ]; then
        pid=`awk '{print $1}' "$XDMPID"`
        if kill -0 "$pid" 2>/dev/null; then
            return 0
        fi
    fi
    ps -e | awk '$NF == "xdm" { found=1 } END { exit !found }'
}

backup_sactab()
{
    stamp=`date '+%Y%m%d-%H%M%S'`
    backup="/root/xdm-sactab-$stamp-$$"
    cp "$SACTAB" "$backup"
    echo "SAF backup: $backup"
}

set_boot_enabled()
{
    if grep '^xdm:xdm::' "$SACTAB" >/dev/null 2>&1; then
        return 0
    fi
    grep '^xdm:xdm:x:' "$SACTAB" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "ERROR: Cannot identify the xdm entry in $SACTAB" >&2
        exit 1
    fi
    backup_sactab
    sed 's/^xdm:xdm:x:/xdm:xdm::/' < "$SACTAB" > "$TMP"
    grep '^xdm:xdm::' "$TMP" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "ERROR: Refusing an invalid SAF update." >&2
        exit 1
    fi
    chmod 644 "$TMP"
    mv "$TMP" "$SACTAB"
}

set_boot_disabled()
{
    if grep '^xdm:xdm:x:' "$SACTAB" >/dev/null 2>&1; then
        return 0
    fi
    grep '^xdm:xdm::' "$SACTAB" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "ERROR: Cannot identify the xdm entry in $SACTAB" >&2
        exit 1
    fi
    backup_sactab
    sed 's/^xdm:xdm::/xdm:xdm:x:/' < "$SACTAB" > "$TMP"
    grep '^xdm:xdm:x:' "$TMP" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "ERROR: Refusing an invalid SAF update." >&2
        exit 1
    fi
    chmod 644 "$TMP"
    mv "$TMP" "$SACTAB"
}

show_status()
{
    if grep '^xdm:xdm:x:' "$SACTAB" >/dev/null 2>&1; then
        echo "Boot mode: manual startxrtg (xdm disabled at boot)"
    elif grep '^xdm:xdm::' "$SACTAB" >/dev/null 2>&1; then
        echo "Boot mode: xdm"
    else
        echo "Boot mode: UNKNOWN"
    fi
    sacadm -l -p xdm
}

case "$1" in
status)
    show_status
    ;;

enable)
    if [ ! -f /usr/X/lib/xdm/Xsetup_0 ] || \
       [ ! -x /usr/X/lib/xrtg/xrtg-session ]; then
        echo "ERROR: Install the Xrtg xdm files before enabling xdm." >&2
        exit 1
    fi

    if strings -a /usr/X/bin/xdm 2>/dev/null | grep 'XRTG_SECURE_COOKIE_V1' >/dev/null 2>&1; then
        :
    else
        echo "ERROR: /usr/X/bin/xdm is not the Xrtg secure-cookie build." >&2
        exit 1
    fi
    if [ ! -f /usr/X/lib/xdm/Xrtg-seed ] || [ "`wc -c < /usr/X/lib/xdm/Xrtg-seed`" -ne 32 ]; then
        echo "ERROR: /usr/X/lib/xdm/Xrtg-seed must contain exactly 32 bytes." >&2
        exit 1
    fi
    set -- `ls -ld /usr/X/lib/xdm/Xrtg-seed`
    if [ "$1" != "-rw-------" ] || [ "$3" != "root" ]; then
        echo "ERROR: Xrtg-seed must be root-owned, mode 600, and not a symlink." >&2
        exit 1
    fi

    if xdm_running; then
        :
    else
        if [ -f /tmp/.X0-lock ] || ps -e | grep 'Xrtg' >/dev/null 2>&1; then
            echo "ERROR: Another X server is using :0." >&2
            echo "       Exit the manual startxrtg session first." >&2
            exit 1
        fi
    fi

    set_boot_enabled
    pmadm -d -p screens -s con10 >/dev/null 2>&1 || \
        echo "WARNING: Could not disable screens/con10 (it may already be disabled)."

    if xdm_running; then
        echo "xdm is already running; boot mode is now enabled."
    else
        sacadm -s -p xdm
        echo "xdm start requested through SAF."
    fi
    show_status
    ;;

disable)
    # Change the next-boot state first.  Even if stopping the live service
    # fails, the machine will come back in manual mode after a reboot.
    set_boot_disabled

    if xdm_running; then
        sacadm -k -p xdm
        attempts=0
        while xdm_running && [ "$attempts" -lt 15 ]; do
            sleep 1
            attempts=`expr "$attempts" + 1`
        done
        if xdm_running || ps -e | grep 'Xrtg' >/dev/null 2>&1; then
            echo "ERROR: X is still stopping; do not start startxrtg yet." >&2
            exit 1
        fi
    else
        echo "xdm is not running."
    fi

    pmadm -e -p screens -s con10 >/dev/null 2>&1 || \
        echo "WARNING: Could not enable screens/con10 (it may already be enabled)."

    echo "xdm is disabled at boot."
    echo "Manual session command: /usr/X/bin/startxrtg"
    show_status
    ;;

*)
    usage
    ;;
esac
