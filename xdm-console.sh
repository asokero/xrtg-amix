#!/bin/sh
# Temporary switch to the native console. Does not alter SAF boot flags.
# Run as root OUTSIDE the XDM/Xrtg session being stopped.
# Optional argument: an absolute path to a trusted passthrough restore tool.
# The default relies on Xrtg's own va2000CloseHW passthrough restoration.
# The future greeter launcher must detach and close inherited XDM descriptors.

PATH=/usr/X/bin:/usr/bin:/usr/sbin:/usr/ucb:/bin
export PATH
umask 077

fail()
{
    echo "xdm-console: $1" >&2
    exit 1
}

if [ "`whoami`" != root ]; then
    fail "must run as root"
fi
if [ "$#" -gt 1 ]; then
    fail "usage: xdm-console [absolute-restore-tool]"
fi
RESTORE=${1:-}
case "$RESTORE" in
'') ;;
/*) [ -f "$RESTORE" ] && [ -x "$RESTORE" ] || fail "restore tool unavailable" ;;
*) fail "restore tool path must be absolute" ;;
esac

# Do not use xdm-mode disable: that also changes the next boot's default.
echo "xdm-console: requesting temporary XDM stop"
/usr/sbin/sacadm -k -p xdm || fail "SAF stop request failed; restore not run"

attempt=0
while :
do
    processes=`/usr/bin/ps -e`
    if [ $? -ne 0 ]; then
        fail "cannot inspect processes; restore not run"
    fi
    # Match executable names, never command arguments containing this helper.
    # Be conservative: any Xrtg or xdm still alive prevents hardware writes.
    echo "$processes" | /usr/bin/awk '
        $NF == "Xrtg" || $NF == "xdm" { running=1 }
        END { exit running ? 0 : 1 }'
    state=$?
    case "$state" in
    1) break ;;
    0) ;;
    *) fail "process check failed; restore not run" ;;
    esac
    if [ "$attempt" -ge 30 ]; then
        fail "XDM/Xrtg did not stop within 30 seconds; restore not run"
    fi
    sleep 1
    attempt=`expr "$attempt" + 1`
done

if [ -n "$RESTORE" ]; then
    echo "xdm-console: running passthrough restore"
    "$RESTORE" || fail "passthrough restore failed; use the remote console"
else
    echo "xdm-console: relying on Xrtg's normal passthrough restoration"
fi
echo "xdm-console: XDM/Xrtg stopped; boot configuration unchanged"
echo "Return to XDM with: /usr/sbin/sacadm -s -p xdm"
exit 0
