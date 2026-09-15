#!/bin/sh
# xrtg-session.sh - the user session controlled by xdm.
#
# The selected WM is the session controller.  Exiting it ends the session; xdm
# then resets Xrtg and presents a fresh login window.

PATH=/usr/X/bin:/usr/bin/X11:/usr/bin:/bin
export PATH

LIBDIR=/usr/X/lib/xrtg
CONF="$LIBDIR/session.conf"
LOCALCONF="$LIBDIR/session.conf.local"

if [ -f "$CONF" ]; then
    . "$CONF"
fi
if [ -f "$LOCALCONF" ]; then
    . "$LOCALCONF"
fi

if [ -z "$XRTG_ROOT_COLOR" ]; then
    XRTG_ROOT_COLOR="#1b2129"
fi

case "$1" in
failsafe)
    exec /usr/X/bin/xterm -geometry 80x24-0-0 -ls
    ;;
esac

TWM=/usr/X/bin/twm
TWMRC="$LIBDIR/twmrc"
XDEFAULTS="$LIBDIR/Xdefaults"

# XRTG chooser begin. Keep TWM and the local application layout unchanged.
# An explicit login argument overrides the user's one-line preference, which
# overrides the system XRTG_SESSION setting. Never source the preference file.
SESSION=${XRTG_SESSION:-default}
if [ -n "$HOME" ] && [ -f "$HOME/.xrtg-session" ]; then
    SESSION=`sed -n '1p' "$HOME/.xrtg-session"`
fi
if [ -n "$1" ]; then
    SESSION=$1
fi

session_failure()
{
    echo "xrtg-session: $1; opening failsafe terminal" >&2
    exec /usr/X/bin/xterm -geometry 80x24-0-0 -ls \
        -title "Xrtg: $1 - exit to retry login"
    exit 1
}

case "$SESSION" in
''|default)
    SESSION=default
    WM="$TWM"
    ;;
amiwm)
    WM=/usr/local/bin/start-amiwm
    ;;
openlook)
    WM=/usr/X/bin/olwm
    if [ ! -x /usr/X/bin/olwsm ]; then
        session_failure "workspace manager missing: /usr/X/bin/olwsm"
    fi
    ;;
*)
    session_failure "unknown session (use default, amiwm or openlook)"
    ;;
esac

if [ ! -x "$WM" ]; then
    session_failure "window manager missing: $WM"
fi

# Keep a long-lived client connected before xrdb or xv exits.  Otherwise the
# X server can reset and discard the resources and root pixmap between those
# short-lived clients and the start of the real session.
case "$SESSION" in
openlook)
    # AT&T olwm uses its own resources, not twm's -f configuration.
    # Xservers includes the Xol font directory before this client starts.
    "$WM" &
    ;;
amiwm)
    # The launcher execs amiwm. Do not use nohup or a transient remote shell:
    # Keyboard/Launcher modules need the lifetime of this XDM session.
    # Inherit XDM's error output, DISPLAY and XAUTHORITY.
    "$WM" &
    ;;
default)
    "$WM" -f "$TWMRC" > /tmp/xdm-twm.log 2>&1 &
    ;;
esac
WMPID=$!
sleep 2

kill -0 "$WMPID" 2>/dev/null
if [ $? -ne 0 ]; then
    wait "$WMPID"
    session_failure "$SESSION exited during startup"
fi
# XRTG chooser end.

if [ -f "$XDEFAULTS" ]; then
    /usr/X/bin/xrdb -load "$XDEFAULTS"
fi

if [ "$SESSION" = default ] && [ -n "$XRTG_BACKDROP" ]; then
    if [ -f "$XRTG_BACKDROP" ] && [ -x /usr/X/bin/xv ]; then
        /usr/X/bin/xv -root -quit "$XRTG_BACKDROP"
    else
        echo "xrtg-session: backdrop unavailable: $XRTG_BACKDROP" >&2
        /usr/X/bin/xsetroot -solid "$XRTG_ROOT_COLOR"
    fi
else
    /usr/X/bin/xsetroot -solid "$XRTG_ROOT_COLOR"
fi

/usr/X/bin/xsetroot -cursor_name left_ptr 2>/dev/null

nohup /usr/X/bin/xterm -geometry 80x24+20+20 \
    > /tmp/xdm-xterm.log 2>&1 &
sleep 1
nohup /usr/X/bin/xclock -geometry 100x100-2-2 \
    > /tmp/xdm-xclock.log 2>&1 &
sleep 1

if [ -n "$XRTG_XMODMAP" ] && [ -f "$XRTG_XMODMAP" ]; then
    /usr/X/bin/xmodmap "$XRTG_XMODMAP"
fi

if [ "$SESSION" = openlook ]; then
    # Workspace Exit ends olwsm but can leave olwm alive. Control the XDM
    # session with the workspace manager, not the remaining window manager.
    # Do not invoke olinit or source .olinitrc here (it may start another WM).
    /usr/X/bin/olwsm &
    WSMPID=$!
    trap 'kill -15 "$WSMPID" "$WMPID" 2>/dev/null; exit 1' 1 2 15
    sleep 2
    if kill -0 "$WSMPID" 2>/dev/null; then
        wait "$WSMPID"
        SESSION_STATUS=$?
        trap 1 2 15
        kill -15 "$WMPID" 2>/dev/null
        # Do not wait indefinitely for olwm. XDM resets the display and
        # cleans up the session after this shell returns.
        exit "$SESSION_STATUS"
    fi
    wait "$WSMPID"
    trap 1 2 15
    kill -15 "$WMPID" 2>/dev/null
    session_failure "olwsm exited during startup"
fi

wait "$WMPID"
exit $?
