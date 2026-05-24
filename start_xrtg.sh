#!/bin/sh
#
# start_xrtg.sh — Start Xrtg and a basic X session on AMIX SVR4.
#
# Run as root from telnet or a console shell.  All clients are launched
# with nohup so they survive when the calling shell session closes.
#
# Adjust geometry and client list to taste.
#
# IMPORTANT: If input (keyboard/mouse) stops working after a server
# crash or kill -9, do a full cold-boot (power off, wait 10 s, power on).
# A soft reset is sometimes sufficient but not reliable.  See TROUBLESHOOTING.md.
#

PATH=/usr/X/bin:/usr/bin/X11:/usr/bin:/bin
DISPLAY=:0
export PATH DISPLAY

# Remove stale lock files from a previous run
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 2>/dev/null

# Start the server; log to /tmp for easy inspection
nohup /usr/bin/X11/Xrtg :0 -pn > /tmp/xrtg_run.log 2>&1 &
XPID=$!
echo "Xrtg pid=$XPID"

# Wait for the server to initialise before starting clients
sleep 4

# Window manager
nohup /usr/X/bin/twm > /tmp/twm.log 2>&1 &
sleep 2

# Sample clients
nohup /usr/X/bin/xclock -geometry 100x100+650+450 > /tmp/xclock.log 2>&1 &
sleep 1
nohup /usr/X/bin/xterm -geometry 70x20+20+20  > /tmp/xterm1.log 2>&1 &
sleep 1
nohup /usr/X/bin/xterm -geometry 70x20+20+280 > /tmp/xterm2.log 2>&1 &

echo "done"
