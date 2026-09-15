# An 8bpp PseudoColor visual: what it would take

Not built.  This is the record of what was established while deciding not to
build it yet, so that the decision can be revisited without redoing the work.

The question came from the OpenTTD port, whose client is fundamentally an
8bpp program: it renders into a chunky palette-indexed buffer and pushes
rectangles with PutImage.

## Why it matters to that workload, in one number

OpenTTD animates a small range of palette entries continuously, for water and
lights.  On a TrueColor visual a palette change invalidates every pixel on
screen -- the pixel values encode the old colours -- so the whole screen must
be converted and pushed again, about eight times a second, whatever the game
is doing.  Measured by the port: 94% of all pixels it pushes come from this.

On PseudoColor the same event is a few XStoreColors and redraws nothing,
because the pixels are indices and the indices did not change.  With their
client fixed to stop redrawing unconditionally, they measure 166 whole-screen
pushes falling to 1 over twenty seconds.

## What fits at depth 16, and why it is not urgent

**None of this table has met the hardware.**  It is arithmetic over measured
parts, and the parts are further apart from the conclusion than they look.
0.42 us/px is itself a subtraction: the measured 2.02 us/px for PutImage minus
an inferred ~1.6 for socket transport, where the inference came from the image
size divided by an observed socket throughput.  The push counts are measured,
but on a development host under Xephyr, not on the Amiga.

It is presented here as settled because it was the basis for not building the
feature, and it is worth being clear that the case against building was no
better evidenced than the case for.  Both are honest arithmetic over measured
parts; neither has been run.

Using 0.42 us/px, which is what remains of the measured 2.02 us/px PutImage
cost once MIT-SHM removes the ~1.6 of socket transport.  Seconds of VRAM
writing per second of wall clock, from whole-screen pushes alone:

|          | 8 screens/s (default) | 2 screens/s (palfreq=16) |
|----------|-----------------------|--------------------------|
| 640x480  | 1.03  does not fit    | 0.26  fits               |
| 400x300  | 0.40  fits            | 0.10  fits               |
| 320x240  | 0.26  fits            | 0.065 fits               |

### Better: the same question with nothing derived in it

The table above multiplies a derived per-pixel cost by measured push counts,
and 640x480 at the default rate fails it by 3%.  A conclusion with that margin
cannot rest on a subtraction.  The port reframed it as a budget -- for each
configuration, the server-side cost per pixel at which it reaches 1.0 s/s --
which is pure arithmetic over counts they measure:

|          | 8 screens/s | palfreq=16 |
|----------|-------------|------------|
| 640x480  | 0.407 us/px | 1.63 us/px |
| 400x300  | 1.04        | 4.17       |
| 320x240  | 1.63        | 6.51       |

A configuration fits if the machine's real per-pixel cost is under its cell,
less about 7% for non-palette drawing.

And there is a floor to compare against that is also underived.  The least a
PutImage can cost server-side, once MIT-SHM has removed the socket, is the
time to write the pixels into VRAM.

Get the units right, because this was got wrong twice.  `va2000_bench` never
prints MB/s; it reports KB/s with KB = 1024, via `kb = bytes / D1024`.  The
raw measurement is **4,872 KB/s** for 32-bit stores (saku26-amix-demo,
docs/MEASUREMENTS.md), which is 4,988,928 bytes/s, and at two bytes a pixel:

    floor = 0.4009 us/px

The first version of this note said 0.411, from "4.87 MB/s" read as decimal.
That 4.87 was itself a binary KB divided by 1000.  Two conversion errors
stacked, and together they moved the floor just past the one budget cell that
was close enough to care -- which is how a 2.4% arithmetic slip turned into a
confident wrong conclusion.

    config          budget    floor     verdict at the floor
    640x480  8/s    0.407     0.4009    OPEN -- 1.5% under, undecided
    640x480  pf16   1.63      0.4009    fits, 4.1x margin
    400x300  8/s    1.04      0.4009    fits, 2.6x margin
    320x240  8/s    1.63      0.4009    fits, 4.1x margin

Three cells are decided on an argument with no subtraction in it.  The fourth
is not: a floor 1.5% under a budget says only that the configuration is not
impossible, and the real cost sits above the floor by an unknown amount that
is very likely more than 1.5%.  Expectation is that 640x480 at the default
rate does not fit; only the end-to-end measurement can say so.

This also settles whether MIT-SHM saves anything server-side.  If it saved
nothing, the whole 2.02 us/px would be server work -- about 1 MB/s of VRAM
writing, against a card measured at 4.87.  Not credible, so the ~1.6
attributed to transport is mostly real.

So the workload fits at full resolution with a client-side setting that
quarters the animation rate.  8bpp buys full-rate animation at full
resolution, not the ability to run -- for this client.  That last qualifier
matters: the lever exists because OpenTTD's full-screen redraws are *only*
palette animation.  An application that redrew for other reasons would have
no such escape, and this conclusion should not be generalised to it.

## The hardware facts, verified

These were the blocker.  They are no longer unknown.  Verified by reading the
sources, not from description:

`wolf3d-amix`: `master/id_vl_amix.c` in the separately maintained project.

- CLUT, one 16-bit write per component, indexed by palette entry (lines
  117-120).  Components arrive as 0-255; how many bits the FPGA latches is
  still unknown and would need a ramp written and photographed.

      R at board + 0x200 + idx*2
      G at board + 0x400 + idx*2
      B at board + 0x600 + idx*2

- Colour mode register 0x0e: 0 is 8bpp, 1 is the RGB565 we use.
- Pitch register 0x58 is in 16-bit words *even in 8bpp*: width/2.  This is the
  one that would silently halve or double the stride if assumed.
- Register 0x16 must be 0x0a in 8bpp or the sync flickers.
- Framebuffer still at board + 0x10000, one byte per pixel, stride = width.

`va2000-amix`: `tools/va2000_restest.c` in the separately maintained project.

- Lines 81-82: 640x480 8bpp and 800x600 8bpp exist as unscaled modes with the
  same timings as their 16-bit counterparts.  A full-resolution 8bpp screen is
  reachable, not only the scaled 320x200 one.
- Line 15: "8bpp note: CPU fills are used (blitter is unreliable in 8bpp
  mode)."  This costs less here than it sounds -- SolidRect already uses CPU
  fills because the blitter hung on it, and at 8bpp a CPU fill moves twice the
  pixels per bus cycle.

## What building it would involve

The colormap and mode work is now small.  The bulk is the drawing layer:
va2000draw.c, va2000text.c and va2000tile.c are written in `unsigned short`
throughout, about a hundred occurrences.

The right approach is what cfb does with PSZ: compile the same sources once
per depth rather than write a second layer by hand.  That turns it into a
build-system change plus a typedef, and it solves the coexistence problem in
the same move -- because 8bpp cannot replace the 16-bit mode.  This server
drives twm, xv and an OPEN LOOK session, and 256 colours is a regression for
all of them.  The shape is a `-depth 8` startup flag with both depths in one
binary.

`va2000screen.c:629` currently sets `numDepths = 1` and `numVisuals = 1`, so
the visual and depth plumbing is also single-valued today and would have to
grow.

## Two operational notes, from the port's own mistakes

Worth having before implementing, both of which cost them a wrong conclusion:

- A private colormap must be *installed* to be visible, and installing it is
  the window manager's job.  Under a bare server the colours are simply wrong
  rather than slow, which reads like a rendering bug in the server and is not
  one.
- Capturing the root window proves nothing about colormap animation when the
  client holds a private AllocAll colormap.  The capture has to be by window
  id; the root's colormap never changes.

## The port has another route to 8bpp that does not involve this server

Worth recording, because it changes who the question belongs to.  The OpenTTD
port plans VA2000-direct and native-chipset backends, which are 8bpp by
nature.  So the 8bpp benefit is not gated on Xrtg growing a PseudoColor
visual; it is gated on work they intend to do anyway, by a path that does not
go through X at all.

Their ordering is X11 first, because that is what runs on a stock machine.
But it means an 8bpp visual here would be one of two ways to the same place
for that client, rather than the only one -- which is a further argument for
not building it on their account, and leaves the case for building it resting
on some future client that needs 8bpp *through X*.

## The two walls are one wall, and they trade

The framing above -- and the one I gave in conversation -- treated "the
simulation is the bottleneck" and "the display is the bottleneck" as rival
answers, with the corollary that if the simulation won then no driver work
mattered.  That is wrong, and the port found it.

OpenTTD checks its palette every `palfreq` ticks, so whole-screen pushes are
**per tick**, not per second.  The 8/s in the table is 33.15 ticks/s divided
by palfreq 4.  It is not a property of the display; it is a property of how
fast the simulation runs.  So the display budget is a function of the tick
rate:

    33.15 ticks/s -> 0.393 us/px   below the 0.4009 floor
    32.48         -> 0.401         exactly at it
    30            -> 0.434         above
    20            -> 0.651         above

Two consequences.

**There is no cliff.**  A display that cannot keep up produces fewer ticks and
therefore fewer palette redraws, so the system settles at a tick rate instead
of collapsing.  Nothing in any of these measurements should have looked like a
threshold, and the "does not fit" language throughout this file is too strong:
a configuration that does not fit at 33 ticks/s is one that runs at 28.

**Driver work is never irrelevant.**  Display and simulation do not compete
for the title of bottleneck, they compete for the same 30 ms frame.  Making
PutImage faster raises the rate the machine settles at, whatever the split.
The disjunction I offered -- if the simulation is the wall, stop optimising
the driver -- would have argued for stopping work on the right thing.

For scale at the floor: one 640x480 push is 123 ms, so at palfreq=4 that is
30.8 ms per tick against a 30.2 ms frame -- the whole frame before the game
has done anything -- and 7.7 ms, or 25%, at palfreq=16.

The crossing at 32.48 ticks/s is not arbitrary.  It is the rate at which
whole-screen pushes alone consume the entire frame.

## What would change the decision

The port's first run on real hardware.  Their client now reports ticks
achieved against the 33/s the game loop targets, and the split between
simulation and blitting.  If the simulation turns out to be the wall on a
68060, then the 94% figure is true and irrelevant, and neither 8bpp nor any
further work on the drawing paths decides whether the game is playable.
