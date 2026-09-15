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

# Window manager.  Override from the environment like everything else here:
#
#   WM=/usr/X/bin/olwm startxrtg      the AT&T OPEN LOOK window manager
#
# olwm is installed on this machine and every library it needs resolves
# (libXol, libXt, libX11).  Its menus are built in and its app-defaults
# already set moveOpaque false, which is what you want on this display.  It
# needs the Xol fonts, which is why they are in FONTPATH below.
WM=${WM:-/usr/X/bin/twm}

# X server binary, display number, video mode and extra server options.
#
# All four can be overridden from the environment without editing this file,
# which is what makes an A/B test practical -- same session, same clients,
# one thing changed:
#
#   XSERVER=/usr/bin/X11/Xrtg.phase3b startxrtg
#   XOPTS=-compat startxrtg
#   MODE=1280x720 XSERVER=/usr/bin/X11/Xrtg.blitmacro startxrtg
#
# Supported modes: 640x480 800x600 1024x768 1280x720.
# 1280x1024 and 1920x1080 need more of the board mapped than the server maps
# today; it refuses them with a message rather than misbehaving.
XSERVER=${XSERVER:-/usr/bin/X11/Xrtg}
MODE=${MODE:-}
XOPTS=${XOPTS:-}

# The display the server owns.  NOT taken from $DISPLAY, and the difference
# matters: a login shell on this machine already exports DISPLAY=unix:0, and
# handing that to the server as its first argument gets you the usage message
# and nothing else -- "unix:0" is not an option and does not begin with ':'.
# The server wants ":0"; the clients want a DISPLAY, and it is set from this
# further down.
XDISPLAY=${XDISPLAY:-:0}

# Font path, passed to the server with -fp.
#
# This is the compiled-in default from Project.tmpl -- misc, Speedo, 75dpi,
# 100dpi -- with /usr/X/lib/fonts/Xol added.  Xol holds the 48 b&h lucida
# faces the OPEN LOOK toolkit asks for, and it is not in the built-in path,
# so without this olwm and the OLIT clients come up unable to find a font.
#
# It has to be a server argument rather than an "xset +fp" later: the window
# manager starts before any short-lived client, so a font path set after it
# would be set too late for the one program that needs it.
FONTDIR=/usr/X/lib/fonts
FONTPATH=${FONTPATH:-$FONTDIR/misc/,$FONTDIR/Speedo/,$FONTDIR/75dpi/,$FONTDIR/100dpi/,$FONTDIR/Xol/}

# Where the server's own output goes.  One log per binary, so an A/B pair
# does not overwrite the evidence from the run before it.
LOGFILE=${LOGFILE:-/tmp/xrtg-`basename $XSERVER`.log}

# =============================================================================
# Session clients
#
# Edit start_clients() to suit your needs.  Each client should be started
# with nohup and & so it survives if the calling shell exits.
# =============================================================================

# Root window appearance.  ROOTCMD runs before the clients; override it from
# the environment like anything else here.
#
#   ROOTCMD='xsetroot -solid \"#1b2129\"'              flat colour
#   ROOTCMD='xsetroot -mod 8 8 -fg \"#222a34\" -bg \"#1b2129\"'   faint grid
#   ROOTCMD='xv -root -quit /root/backdrop.gif'                   an image
#
# For an image, prepare it on a fast machine and ship the result.  Two things
# decide how long it takes to appear, and the format is the bigger one:
#
#   GIF   56 KB   0.03 s off disk, LZW decode -- the one to use
#   JPEG  98 KB   0.06 s off disk, but DCT decode is seconds on a 68060
#   PPM  2.7 MB   no decode at all, and 1.6 s of disk at the measured 1.75 MB/s
#
# GIF wins because it is small AND cheap to decode, and 256 colours costs
# nothing on anything that is not a photograph.  Scale to exactly the screen
# size first so xv does not resample at load time.  The recipe, for a 1280x720
# screen and the background colour this session uses:
#
#   magick in.jpg -resize 1280x -background '#1b2129' \
#          -gravity center -extent 1280x720 -strip -colors 256 backdrop.gif
#
# -resize 1280x rather than 1280x720 keeps the aspect ratio and lets -extent
# centre it, which is what a banner wants; forcing 16:9 would crop or stretch.
#
# The loading cost is paid once.  The other one is not: every time a window
# uncovers the desktop the server repaints that area from the pixmap, and a
# host-to-VRAM copy on this card measures 0.463 us/pixel, so a full-screen
# repaint is about 0.43 s whatever the file format was.  A solid colour goes
# to the blitter instead and is free.  That is the real price of a backdrop
# here, and no amount of preparing the file changes it.
#
# Keep the inner quotes.  This value is run through sh -c, and there a bare
# #1b2129 starts a word, which makes it a comment: the colour vanishes and
# xsetroot says "-solid requires an argument".  Quoted, it survives.
#
# A tiled or image backdrop needs the server to paint window backgrounds
# from the pixmap rather than approximating it with one pixel, which is what
# the tile work on this branch fixed.  Against an older server the backdrop
# turns flat the first time a window moves across it.
ROOTCMD=${ROOTCMD:-'xsetroot -solid "#1b2129"'}

start_clients() {
    # Resources first, so the clients started below pick them up.  This runs
    # after the window manager is already connected -- see the comment at the
    # call site for why that ordering is load-bearing.
    if [ -f /root/.Xdefaults ]; then
        xrdb -load /root/.Xdefaults && echo "Resources loaded from /root/.Xdefaults"
    fi

    # Root window, then a normal arrow instead of the X.
    sh -c "$ROOTCMD" 2>/dev/null
    xsetroot -cursor_name left_ptr 2>/dev/null

    nohup xterm  -geometry 80x24+20+20  > /tmp/xterm1.log  2>&1 &
    sleep 1
    nohup xterm  -geometry 80x24+20+380 > /tmp/xterm2.log  2>&1 &
    sleep 1
    nohup xclock -geometry 100x100-2-2 > /tmp/xclock.log 2>&1 &
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
DISPLAY=$XDISPLAY
export PATH DISPLAY

# Remove stale lock files from a previous run.
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 2>/dev/null

# Start the X server.  nohup keeps it alive if the calling shell exits.
if [ -n "$MODE" ]; then
    echo "  running: $XSERVER $XDISPLAY -mode $MODE -pn $XOPTS"
    nohup "$XSERVER" "$XDISPLAY" -mode "$MODE" -pn -fp "$FONTPATH" $XOPTS \
        > "$LOGFILE" 2>&1 &
else
    echo "  running: $XSERVER $XDISPLAY -pn $XOPTS"
    nohup "$XSERVER" "$XDISPLAY" -pn -fp "$FONTPATH" $XOPTS \
        > "$LOGFILE" 2>&1 &
fi
XPID=$!
echo "Xrtg started  (pid $XPID, log $LOGFILE)"

# Wait for the server to initialise.
sleep 4

# Check the server came up.
kill -0 "$XPID" 2>/dev/null || {
    echo "ERROR: Xrtg did not start." >&2
    echo "First lines of $LOGFILE:" >&2
    head -5 "$LOGFILE" >&2
    echo "(A usage message here means the server did not like an argument," >&2
    echo " and the first argument is the one to suspect.)" >&2
    exit 1
}

# The board line the server logs at startup: which bus was detected, which
# fast paths are enabled.  Worth seeing every time, and the first thing to
# quote when something looks wrong.
grep "^va2000:" "$LOGFILE" 2>/dev/null

# Verify the chosen window manager exists before trying to run it.
if [ ! -x "$WM" ]; then
    echo "ERROR: Window manager not found: $WM" >&2
    echo "       Edit WM= at the top of this script." >&2
    kill "$XPID" 2>/dev/null
    exit 1
fi

# Start the window manager FIRST, in the background, and only then set up
# resources and start clients.
#
# The order matters and it is not a style question.  An X server resets when
# its last client disconnects, and a reset throws away the root window's
# properties along with everything else.  Run xrdb before any long-lived
# client and this is what happens: xrdb connects, sets RESOURCE_MANAGER,
# exits -- no clients left, reset, resources gone.  Then xsetroot paints the
# backdrop, exits, reset, backdrop gone.  The session that finally comes up
# has neither, and on this machine it also comes up with no keyboard or
# mouse, because the screen manager hands input to the X screen once at probe
# time and the probe does not run again after a reset.
#
# The window manager is a long-lived client, so starting it first holds the
# connection open and there is no reset to lose anything to.
echo "Starting $WM -- use its Exit menu to shut down the X session."
"$WM" > /tmp/wm.log 2>&1 &
WMPID=$!

# Give it a moment to fail, if it is going to.  twm reads /root/.twmrc at
# startup and exits on a syntax error.
sleep 2
kill -0 "$WMPID" 2>/dev/null || {
    echo "ERROR: $WM exited immediately." >&2
    WMPID=""
}

# Resources, backdrop and clients, with the connection held by the WM.
start_clients

# Block until the window manager exits.
if [ -n "$WMPID" ]; then
    wait "$WMPID"
fi

# A window manager that exits at once did not exit, it failed.  twm reads
# /root/.twmrc at startup and quits on a syntax error, naming the line -- but
# that message goes to its log, where nobody looks, and all the session shows
# is X coming straight back down.  Put it on the terminal instead.
if [ -s /tmp/wm.log ]; then
    echo ""
    echo "--- $WM said (/tmp/wm.log) ---"
    cat /tmp/wm.log
    echo "-------------------------------"
    echo "A syntax error names the line in /root/.twmrc.  To get back to a"
    echo "working session: mv /root/.twmrc /root/.twmrc.broken  (twm then"
    echo "uses its built-in defaults) or restore /root/.twmrc.bak."
    echo ""
fi

# =============================================================================
# Cleanup -- runs when the window manager exits
# =============================================================================

echo "Window manager exited -- shutting down X."
kill "$XPID" 2>/dev/null
sleep 3
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 2>/dev/null

# The server prints its counters as it shuts down: how often each fast path
# applied, and whether the blitter ever made it wait.  These are what turn
# "it felt slow" or "it stopped for a second" into something actionable.
echo ""
echo "--- session summary ($LOGFILE) ---"
grep "^va2000:" "$LOGFILE" 2>/dev/null
grep "slow blit" "$LOGFILE" 2>/dev/null
echo "Done."
