#!/bin/sh
# start_xrtg.sh — Start Xrtg and an X session on AMIX SVR4.
#
# The window manager runs in the foreground and acts as the session
# controller.  When it exits (via its own menu), the X server is shut
# down cleanly.
#
# Normal use — session ends when the window manager exits:
#   sh start_xrtg.sh
#
# Keep the session alive after closing the telnet connection:
#   nohup sh start_xrtg.sh > /tmp/xsession.log 2>&1 &
#
# Must be run as root.
#
# IMPORTANT: If input (keyboard/mouse) stops working after a server
# crash or kill -9, do a full cold-boot (power off, wait 10 s, power on).
# A soft reset is sometimes sufficient but not reliable.  See TROUBLESHOOTING.md.

# =============================================================================
# Configuration
# =============================================================================

# Window manager.  Uncomment the one you want to use.
WM=/usr/X/bin/twm
# WM=/usr/X/bin/olwm          # OpenLook Window Manager
# WM=/usr/X/bin/tvtwm        # Alternative twm build

# X server binary and display number.
XSERVER=/usr/bin/X11/Xrtg
DISPLAY=:0

# Video mode.  Supported: 640x480 800x600 1024x768 1280x720 1280x1024 1920x1080
# Leave empty to use the default (800x600).
MODE=

# =============================================================================
# Session clients
#
# Edit start_clients() to suit your needs.  Each client should be started
# with nohup and & so it survives if the calling shell exits.
# =============================================================================

start_clients() {
    # Uncomment to set the root window background before starting clients:
    # xsetroot -grey

    nohup xterm  -geometry 70x20+20+20  > /tmp/xterm1.log  2>&1 &
    sleep 1
    nohup xterm  -geometry 70x20+20+280 > /tmp/xterm2.log  2>&1 &
    sleep 1
    nohup xclock -geometry 100x100+650+450 > /tmp/xclock.log 2>&1 &
    sleep 1

    # Apply keyboard layout if /root/.Xmodmap exists.
    # Runs here (after first clients) so STREAMS :0 socket is ready.
    if [ -f /root/.Xmodmap ]; then
        xmodmap /root/.Xmodmap && echo "Keyboard layout applied from /root/.Xmodmap"
    fi
}

# =============================================================================
# Startup
# =============================================================================

PATH=/usr/X/bin:/usr/bin/X11:/usr/bin:/bin
export PATH DISPLAY

# Remove stale lock files from a previous run.
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 2>/dev/null

# Start the X server.  nohup keeps it alive if the calling shell exits.
if [ -n "$MODE" ]; then
    nohup "$XSERVER" "$DISPLAY" -mode "$MODE" -pn > /tmp/xrtg.log 2>&1 &
else
    nohup "$XSERVER" "$DISPLAY" -pn > /tmp/xrtg.log 2>&1 &
fi
XPID=$!
echo "Xrtg started  (pid $XPID, log: /tmp/xrtg.log)"

# Wait for the server to initialise.
sleep 4

# Check the server came up.
kill -0 "$XPID" 2>/dev/null || {
    echo "ERROR: Xrtg did not start -- check /tmp/xrtg.log" >&2
    exit 1
}

# Verify the chosen window manager exists before trying to run it.
if [ ! -x "$WM" ]; then
    echo "ERROR: Window manager not found: $WM" >&2
    echo "       Edit WM= at the top of this script." >&2
    kill "$XPID" 2>/dev/null
    exit 1
fi

# Start session clients.
start_clients

# Start the window manager in the foreground.
# The script blocks here -- to shut down X, use the WM Exit menu entry.
echo "Starting $WM -- use its Exit menu to shut down the X session."
"$WM" > /tmp/wm.log 2>&1

# =============================================================================
# Cleanup -- runs when the window manager exits
# =============================================================================

echo "Window manager exited -- shutting down X."
kill "$XPID" 2>/dev/null
sleep 1
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 2>/dev/null
echo "Done."
