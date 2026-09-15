# Where the optimization branch stands

Written while the machine was off; everything below phase 4 is compiled and
crosschecked but **not yet run on hardware**.

## Measured on the machine

| change | result |
|---|---|
| Phase 1, compiler | no measurable difference; see `phase0-1-z3-060.md` |
| Phase 2, native PutImage | no change -- the path was taken, the cost was elsewhere |
| Phase 3, native glyphs | **text 2.5-4.6x**, 98% of runs take the native path |
| Padded-scanline fix (phase 0) | confirmed in use: resizing an xv image window no longer skews it |
| Bounded blitter wait | 0 timeouts, 0 slow waits in a real session -- the blitter is not stalling |

## Built and crosschecked, not yet measured

| change | expected |
|---|---|
| memcpy -> `va2000CopyRun` | PutImage measured 3.07 us/pixel through memcpy where the board takes 0.79 with plain 16-bit stores. This is the fix for the three quarters the saku26 project reports is left after MIT-SHM. |
| Phase 4, pixmap CopyArea | 30x on that path, measured as the gap. It is the software cursor's path. |
| GraphicsExpose from CopyArea | should end the leftover artefacts after window moves |
| Phase 5, backing store (`-bstore`) | removes redraws rather than speeding them up |
| Phase 6, real aperture | 1280x1024 on either bus, 1920x1080 on Zorro III |
| Phase 7, tile and stipple | correctness; unblocks backdrops and OPEN LOOK |
| Phase 8, session files | `session/Xdefaults`, `session/twmrc` |
| `-blitmin` | now does what it says; default unchanged at 0 |

## Known defect: something writes past the framebuffer

Phase 6 trimmed the mmap from a fixed 2 MB to exactly the framebuffer the
mode needs. At 1280x720 that removed 188 KB of slack that had always been
there, and the server took a **hardware bus error while a window was being
resized**.

A bus error is an access to an unmapped address, so something in the
drawing paths runs past the end of the framebuffer -- and has been doing so
all along, silently, because the slack was mapped. Trimming the mapping did
not cause it; it revealed it.

The mapping is back to the whole aperture so the server is usable. The
defect is still there. What is known:

- It happens during a window resize.
- The same session shows **more visual garbage than before**, which may be
  the same fault landing inside the visible area rather than past it.
- The reads and writes it could be are the ones that take a row at a time:
  `va2000GetSpans` and `va2000CopyRects` are the candidates, since both
  compute a row address from coordinates a caller supplied.

## Open questions, in the order worth answering

1. **The freeze.** A tester reports the desktop stopping briefly, which did
   not happen before this branch. Ruled out: continuous CPU spin (the
   server accrues no CPU while idle) and the blitter wait (zero slow waits
   logged). The best remaining hypothesis is a large PutImage: at 3.07
   us/pixel a 1280x720 image is 2.8 seconds, and the tester was using xv.
   The memcpy fix should settle it either way.
2. **The dispatch floor.** Two measurements point at request handling
   rather than drawing and neither measured it. `xbench`'s new `changegc`
   test does. If it lands near the per-character time, the inner loops are
   finished and further DDX work is not where the time is.
3. **The 2x machine variance.** Consecutive runs of an unchanged binary
   differ by about a factor of two, cause unknown. It sets the floor on
   what can be measured here at all.

## Not started

- Off-screen VRAM pixmaps. The board has 32 MB and the blitter's base
  register reaches all of it. This would turn phase 4, backing store and
  tile fills into blitter operations at once. It touches every place that
  assumes pixmap data is host memory, so it wants the machine available.
- XView and olwm. Phase 7 was its prerequisite.
- MIT-SHM is already compiled in; the saku26 project has taken that side.

## The bus error, and the garbage (2026-09-03, evening)

Two separate defects, found from a photograph of the console and one of the
screen.

### 1. CopyArea read before the framebuffer  (mine, phase 4)

```
WARNING: Hardware Bus Error @ C109BD34, (4000FD34 physical)
NOTICE: User BUS ERROR at C109BD34, PC:80005D32 PID:2360
```

The mapping starts at `C108C000`, so the faulting address is offset `0xFD34`
-- 716 bytes *below* the framebuffer at `0x10000`, i.e. 358 pixels before
pixel (0,0).  `y = -1, x = 922` produces exactly that.  Nothing in the FPGA
decodes that address, hence a bus timeout rather than merely wrong pixels.

`va2000CopyRects` clamped the source rectangle's far edge and not its near
edge.  miDC asks for the area under the cursor without checking whether all
of it exists, so every time the pointer neared the top of the screen the
server read above row 0.  Fixed by clamping both edges and moving the
destination with the source.

This is also what the fixed 2 MB mapping had been hiding: phase 6 trimmed the
mapping to the exact framebuffer size and turned a long-standing overrun into
a fault.  Reverted in c320b87; the mapping stays generous, but the overrun is
now actually gone.

### 2. PolyFillRect ignored the alu  (pre-existing, on main)

`va2000SolidRect`'s CPU path wrote `pGC->fgPixel` whatever the alu said.
Right for GXcopy, wrong for the other fifteen.  GXxor is the one that shows:
twm draws its move and resize outline by XORing thin rectangles onto the root
and erases them by XORing the same rectangles again.  Painting solid both
times draws the outline twice instead of removing it, so a frame stayed on
screen after every drag -- the fragments seen around xclock's title bar and
saku26's resize corner.  Non-copy alu now goes to `miPolyFillRect`, which
returns through `FillSpans`, which does implement all sixteen.

### New flag: -vaopt

`-vaopt <mask>` sets the option word directly, so a fast path can be switched
off one at a time instead of all at once with `-compat`:

| bit | feature |
|-----|---------|
| 0x001 | glyph blitting |
| 0x002 | CopyArea fast path |
| 0x004 | PutImage |
| 0x008 | 32-bit stores |
| 0x010 | async blitter |
| 0x020 | tile and stipple fills |
| 0x040 | backing store |
| 0x080 | off-screen pixmaps |
| 0x100 | stall watch |

`0x01af` is the default.  To bisect a display fault, clear one bit at a time.

### 3. The cursor trail: an overlapping copy in the wrong direction

Reported as smears left in title bars wherever the pointer had travelled.

miDC does not re-save the whole area under the cursor on every move.
`miDCChangeSave` scrolls what it already has, by copying the save pixmap onto
itself:

```c
CopyArea(pSave, pSave, pGC, sourcex, sourcey, copyw, copyh, destx, desty);
```

Source and destination are the same memory and they overlap.  `va2000CopyRects`
copied rows top-down, each row left-to-right, which reads back what it has
already written and smears the leading pixels across the rectangle.  The
corrupted pixmap is then restored to the screen.

Over a plain background it is invisible.  Over a title bar, with text and
buttons to smear, it is the reported debris -- which is why it looked like the
cursor was painting rather than the save being wrong.

`miCopyArea`, which the phase 4 fast path replaced, chose its direction, and
`copyVRAMBox` chooses one for window-to-window.  `va2000CopyRects` is the third
place that has to, and did not.  Fixed: rows moving down copy bottom-up, rows
staying level and moving right copy right-to-left.

The comment above the function had asserted the opposite -- "Overlap is not a
concern here: two different drawables cannot overlap" -- which is true of every
caller except the one that matters most, since it runs on every pointer move.

## Confirmed on the machine (Xrtg.phase13)

Reported after a session at 1280x720, Zorro III, 68060: "X toimii nyt hienosti.
Nopeutta on tullut rutkasti, se vaikuttaa tähän saakka vakaalta ja roskat ja
jumittelut vaikuttavat olevan poissa."

All four defects from this round are closed: the bus error, the XOR drag
outline, the SetSpans raster op, and the cursor trail.  The intermittent freeze
reported against phase 3 has not recurred either -- the blitter counters had
already exonerated the hardware (0 timeouts, 0 slow waits), so the likeliest
explanation is that it was the same overlapping-copy corruption forcing repaint
storms.  Worth keeping an eye on rather than declaring fixed.

Still open: partial planemask in FillSpans and SetSpans; off-screen VRAM
pixmaps; XView/olwm; the session/ appearance files are written but not
installed.


## Instrumentation, and a revised plan (2026-09-04)

### MIT-SHM was already there

Listed as a major remaining item on the strength of a measurement: PutImage
costs 2.02 us/pixel of which ~1.6 is the image going through the socket at
about 1 MB/s.  That measurement stands, but the conclusion drawn from it did
not: the shared-memory extension is already in this server.

`extensions/server/shm.c` and `extensions/lib/XShm.c` are both in the tree,
both gated on `HasShm`, and `Imake.tmpl` defaults `HasShm` to YES on
SystemV4 -- which amix.cf sets.  The generated server Makefile carries
`-DMITSHM` in STD_DEFINES and EXT_DEFINES, and `amixInitExtDMI.c` calls
`ShmExtensionInit()` under that guard.  The saku26 demo confirms it from the
other side: it prints "shared memory, 131072 bytes" against this server.

So the transport cost applies only to clients that do not use SHM.  Nothing
to build.

### PutImage "0 native, 7 via mi" is correct

A real session logged every PutImage falling back to mi, which looked like
the phase 2 work being dead.  It is not: those calls are miDC realizing
cursors, which does PutImage at depth 1 into a depth-1 bitmap.  The native
path is depth 16 and correctly declines.  A session with no image client in
it does no depth-16 PutImage at all.

### What the counters now measure

The unexplained item is the freeze: a real session logged 8 stretches over
200 ms awake, the longest 5450 ms.  A long stretch awake says the server did
not get back to select(); it does not say what it was doing.

The block handler now snapshots the work counters at each waking and reports
the difference, so the line reads

```
va2000: 5450 ms awake: 3 blits (12 polls), fill 40000, copy 0, image 0, glyph 900 px
```

The decisive bit is whether those numbers are large or near zero.  Near zero
means the time went somewhere this driver does not reach -- a font read off
the disk, a server reset, the kernel -- and no amount of drawing optimisation
would touch it.

### What decides the off-screen VRAM pixmap question

Off-screen VRAM pixmaps would let pixmap-to-screen copies use the blitter,
which is about eight times the CPU path (1145-1243 against 95-143 ops/s).
Three things argue for caution:

  - The blitter costs ten register writes across Zorro before it moves a
    pixel, so small copies lose.  miDC's cursor save pixmap is ~16x16, which
    is almost certainly below that line.
  - VRAM is slower than host memory for CPU access, so any pixmap the CPU
    draws into -- which is most of them, and all tiles -- gets slower.
  - The blitter has one row-pitch register, so source and destination must
    share a stride.  Off-screen pixmaps would have to be allocated at screen
    stride, which wastes VRAM, and the mapping ceiling is unknown: 2120 KB
    works, 32 MB fails with ENOMEM.

So the feature is not built.  Instead the session report now prints a
histogram of pixmap copy areas against the current -blitmin.  If real
sessions only ever copy small pixmaps, the feature is not worth building,
and that will be visible in one session.
