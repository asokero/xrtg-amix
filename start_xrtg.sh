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
# WM=/usr/X/bin/twm.r4        # Alternative twm build
# WM=/usr/X/bin/mwm           # Motif Window Manager

# X server binary and display number.
XSERVER=/usr/bin/X11/Xrtg
DISPLAY=:0

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
}

# =============================================================================
# Startup
# =============================================================================

PATH=/usr/X/bin:/usr/bin/X11:/usr/bin:/bin
export PATH DISPLAY

# Remove stale lock files from a previous run.
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 2>/dev/null

# Start the X server.  nohup keeps it alive if the calling shell exits.
nohup "$XSERVER" "$DISPLAY" -pn > /tmp/xrtg.log 2>&1 &
XPID=$!
echo "Xrtg started  (pid $XPID, log: /tmp/xrtg.log)"

# Wait for the server to initialise.
sleep 4

# Check the server came up.
if ! kill -0 "$XPID" 2>/dev/null; then
    echo "ERROR: Xrtg did not start -- check /tmp/xrtg.log" >&2
    exit 1
fi

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
