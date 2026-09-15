#!/bin/sh
# x11perf-baseline.sh -- record an Xrtg performance baseline on AMIX.
#
# Run this against a running Xrtg before and after each optimisation phase.
# Without a baseline the estimates in the review stay estimates, and a
# regression on the other target machine goes unnoticed.
#
# Usage:
#   sh x11perf-baseline.sh [outfile] [label]
#
#   outfile   where to write the report   (default /tmp/xrtg-perf.txt)
#   label     free-text note stored in the header, e.g. "phase0 z3 060"
#
# Compare two runs with:
#   x11perfcomp /tmp/base.txt /tmp/new.txt
#
# Copy the result into perf/ in the repository and commit it together with
# the change it measures.
#
# Notes:
#   - Each test is run in its own x11perf invocation so that one unknown
#     test name costs a single line instead of aborting the whole run.
#   - TESTS below is a starting point.  `x11perf -help` lists every test
#     this build supports; tune the list on the machine and keep it stable
#     afterwards, because only identical lists are comparable.

OUT=${1:-/tmp/xrtg-perf.txt}
LABEL=${2:-unlabelled}

DISPLAY=${DISPLAY:-:0}
export DISPLAY

# Seconds per repetition and number of repetitions.  Lower than the x11perf
# default of 5s/5reps, because the default suite takes most of an hour here.
#
# Three repetitions, not two: the saku26 demo measured the same configuration
# and the same box size at 14, 15, 40, 46 and 47 ms/frame across runs, cause
# unknown.  A single number from this machine means very little, so keep the
# repetitions and read the spread x11perf reports, not just the average.
SECS=3
REPS=3

TESTS="-dot \
-rect1 -rect10 -rect100 -rect500 \
-line10 -line100 \
-circle10 -circle100 \
-scroll10 -scroll100 -scroll500 \
-copywinwin10 -copywinwin100 -copywinwin500 \
-copypixwin100 -copywinpix100 -copypixpix100 \
-putimage10 -putimage100 \
-getimage10 -getimage100 \
-text -ftext"

X11PERF=x11perf
PATH=/usr/X/bin:/usr/bin/X11:/usr/bin:/bin
export PATH

# ---------------------------------------------------------------------------

if [ ! -x /usr/X/bin/x11perf ] && [ ! -x /usr/bin/X11/x11perf ]; then
    echo "ERROR: x11perf not found in /usr/X/bin or /usr/bin/X11." >&2
    echo "       It ships with the X11R5 binary package." >&2
    exit 1
fi

echo "Writing $OUT (label: $LABEL)"
echo "This takes a while -- do not touch the mouse or keyboard."

# --- header ----------------------------------------------------------------

echo "# Xrtg performance baseline"                       >  "$OUT"
echo "# label:   $LABEL"                                 >> "$OUT"
echo "# date:    `date`"                                 >> "$OUT"
# uname -srvm, not -a: -a includes the node name, and these files are
# committed to a public repository.  The system identity that matters
# for a measurement is the release and the machine type, not its name.
echo "# system:  `uname -srvm`"                          >> "$OUT"
echo "# display: $DISPLAY"                               >> "$OUT"
echo "# timing:  $SECS s x $REPS reps per test"          >> "$OUT"

# The board and option line Xrtg logs at startup identifies the
# configuration this run belongs to -- bus mode, firmware, option mask.
if [ -f /tmp/xrtg.log ]; then
    grep "^va2000:" /tmp/xrtg.log > /tmp/xrtg-perf-caps.$$ 2>/dev/null
    if [ -s /tmp/xrtg-perf-caps.$$ ]; then
        sed 's/^/# /' /tmp/xrtg-perf-caps.$$                >> "$OUT"
    fi
    rm -f /tmp/xrtg-perf-caps.$$
fi

# One grep per pattern: this shell's grep has no alternation.
xdpyinfo > /tmp/xrtg-perf-dpy.$$ 2>/dev/null
if [ -s /tmp/xrtg-perf-dpy.$$ ]; then
    grep "dimensions"    /tmp/xrtg-perf-dpy.$$ | sed 's/^ */# /' >> "$OUT"
    grep "depth of root" /tmp/xrtg-perf-dpy.$$ | sed 's/^ */# /' >> "$OUT"
    grep "MIT-SHM"       /tmp/xrtg-perf-dpy.$$ | sed 's/^ */# ext: /' >> "$OUT"
fi
rm -f /tmp/xrtg-perf-dpy.$$

echo "#" >> "$OUT"

# --- tests -----------------------------------------------------------------

for t in $TESTS
do
    echo "  $t"
    $X11PERF -time $SECS -repeat $REPS $t >> "$OUT" 2>&1
    if [ $? -ne 0 ]; then
        echo "# NOTE: $t failed or is not supported by this x11perf" >> "$OUT"
    fi
done

echo "" >> "$OUT"
echo "Done.  Results in $OUT"
echo "Copy it into perf/ in the repository and commit it with the change."
