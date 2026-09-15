# Xrtg — X11R5 RTG Server for Amiga UNIX (AMIX)

## Overview

Xrtg is an X11R5 server for Amiga UNIX (AMIX) SVR4 that drives the MNT VA2000
RTG graphics card directly — bypassing the native Amiga display chipset.

This is my my very (un)reasonable vibe-coding adventure. The goal is to explore,
and see how far the combination of old hardware, old Unix, modern AI tools can 
go.

This is a companion project to
[va2000-amix](https://github.com/asokero/va2000-amix), which provides the
kernel device driver for the VA2000. Xrtg builds on top of that driver to run
a full X11 session in 16-bit TrueColor via the VA2000 framebuffer.

Xrtg is built entirely from the vanilla AMIX X11R5 source tree (Keith
Gabryelski's public domain port of X11R5 to AMIX), with a new RTG DDX layer
and VA2000-specific driver added on top. No external packages were used.

Klaus Burkert's Xsvga (from Gateway! Volume 2) is a parallel project doing
the same thing for Cirrus Logic-based RTG cards. Xsvga demonstrated that
building an RTG X11 server on vanilla AMIX is possible and served as
inspiration for this project.

---

## Status (May 2026)

**Tested hardware (updated September 2026):** Amiga 3000 with 68030, 68040
and 68060, running AMIX SVR4 2.1p2a. Earlier testing used the MNT VA2000 in
Zorro II mode (fw1.9.0b2); 68040 and 68060 configurations have also been tested
with the VA2000 in Zorro III mode.

### Working

- **Server compiles and links** on AMIX SVR4
- **X server starts** — passes font path, initialises screen, accepts connections
- **VA2000 display activates** — RGB565 TrueColor, correct colours
- **Resolution selection** — `-mode WxH` flag selects from six supported modes
  (640×480, 800×600, 1024×768, 1280×720, 1280×1024, 1920×1080); default 800×600
- **Clean framebuffer start** — VRAM cleared to black on init, no garbage pixels
- **Keyboard and mouse input** — routed via the AMIX screen manager
  (`OpenScreen` / `SIOCACTIVATE`); twm responds to mouse and keyboard normally
- **Window manager** — twm starts, window dragging and resizing work
- **Multiple clients** — tested with twm, xclock, xterm (two instances)
- **Hardware blitter — PaintWindow** — window background fills use the VA2000
  hardware blitter (`blitFill`), which is noticeably faster than CPU fills
- **Hardware blitter — CopyWindow** — screen-to-screen window-move copies use
  the VA2000 hardware blitter (`blitCopy`); register layout verified against the
  FPGA Verilog source
- **Software cursor** — miDC cursor rendered correctly (no hardware sprite on
  VA2000)
- **Server reset** — survives client-side server resets across server
  generations

### Not Yet Implemented / Known Issues

- **Hardware blitter — SolidRect** — PolyFillRect (used by widget toolkits for
  button and border fills) falls back to CPU fills. Hardware acceleration here
  caused a BLITWAIT hang during testing; root cause not yet determined.
- **Graphical expose artefacts** — when windows are moved, small remnants can
  appear. They clear when another window is moved over the area or (in xterm)
  when text is typed over it. This is normal X11 behaviour without backing
  store.
- **No hardware cursor sprite** — VA2000 has no hardware cursor; the FPGA
  source contains a placeholder `display_sprite` register that is never driven.

---

## Hardware Requirements

- Amiga with Zorro II or Zorro III slots (tested on Amiga 3000 with
  68030, 68040 and 68060)
- MNT VA2000 graphics card with firmware appropriate for the selected bus mode;
  both Zorro II and Zorro III modes have been tested
- Amiga UNIX (AMIX) System V Release 4.0, version 2.1p2a
  (68040/68060 systems require an AMIX kernel with support for their CPU)
- VA2000 kernel driver installed and `/dev/va2000` accessible
  (see [va2000-amix](https://github.com/asokero/va2000-amix))
- Vanilla AMIX X11R5 source tree installed at `/usr/x11r5`
  (Keith Gabryelski's port, included with AMIX 2.1)

---

## Files

```
start_xrtg.sh                    Start Xrtg and a basic X session (twm + xterm)
install-sources.sh               Install modified sources into /usr/x11r5 tree
install-xrtg.sh                  Install compiled Xrtg binary to /usr/bin/X11
make-release.sh                  Package compiled Xrtg into a binary tar archive

usr/x11r5/
  config/
    amix.cf                      AMIX imake configuration (RTG, BuildPex NO)
    amix.rules                   NormalLibraryTarget without ranlib for SVR4
    Imake.rules                  Imake macro rules
    noop.rules                   No-op rule stubs
    sv4Lib.rules                 SVR4 library rules

  server/
    Imakefile                    Top-level server Imakefile — adds Xrtg target
    ddx/amix/
      Imakefile                  amix DDX Imakefile — adds rtg/ subdir
      amixInit.c                 InitOutput() — RTG device path pre-selection
      amixCursor.c               Cursor init — TIGA guard to avoid link error
      amixIo.c                   ddxProcessArgument — adds -mode WxH flag

    ddx/amix/rtg/
      Imakefile                  RTG layer Imakefile
      rtg.h                      RTG type definitions
      rtgInit.c                  rtgProbe / rtgCreate — screen init, input fd

    ddx/amix/rtg/va2000/
      Imakefile                  VA2000 driver Imakefile
      va2000.h                   Hardware definitions, blitter registers
      va2000hw.c                 Hardware init — mode set, register programming
      va2000screen.c             Screen setup — depth, colormap, GC operations
      va2000draw.c               Drawing ops — blitter fill, CPU spans/copy
      va2000win.c                Window operations
      va2000cmap.c               Colormap management — 16-bit RGB565
      va2000pix.c                Pixmap operations
      va2000cursor.c             Software cursor

  util/makedepend/
    makedepend                   Pre-built makedepend binary (from /usr/X/bin/)
```

---

## Quick Start

### Prerequisites

1. AMIX SVR4 2.1p2a with the vanilla X11R5 source tree at `/usr/x11r5`
2. VA2000 kernel driver installed (`/dev/va2000` exists)
3. This repository transferred to the AMIX machine

### 1. Install sources into X11R5 tree

```sh
cd /usr/x11r5
sh /path/to/install-sources.sh /path/to/xrtg-amix
```

### 2. Build prerequisite library

```sh
cd /usr/x11r5/extensions/server && make
```

`install-sources.sh` builds `fonts/lib/font/libfont.a` and runs `make Makefiles`
in the server tree automatically — these steps are not needed manually.

### 3. Build Xrtg

```sh
cd /usr/x11r5/server
make depend && make Xrtg
```

### 4. Install the binary

```sh
sh /path/to/install-xrtg.sh
```

### 5. Start X

From a root shell (telnet or console):

```sh
startxrtg
```

`install-xrtg.sh` installs `startxrtg` to `/usr/X/bin/`. It starts Xrtg,
waits for it to initialise, then launches twm, xclock, and two xterm windows.
All clients are run with `nohup` so they survive when the calling shell
session closes.

To select a video mode, edit the `MODE=` line in `/usr/X/bin/startxrtg`:

```sh
MODE=1280x720
```

Supported modes: `640x480`, `800x600` (default), `1024x768`, `1280x720`,
`1280x1024`, `1920x1080`.

The server maps what the driver says the board has rather than a fixed
2 MB, so the mode table is no longer limited by the mapping. AutoConfig
reports a 4 MB aperture in Zorro II and 32 MB in Zorro III, which means
`1280x1024` (2560 KB) works on either and `1920x1080` (4050 KB) needs
Zorro III. A mode the board cannot hold is refused with a message naming
both figures.

---

## Runtime options (optimization branch)

Work on the `optimization` branch is guarded by runtime switches rather than
`#ifdef`s, so one binary serves both target machines — a stock A3000UX
(68030, VA2000 in Zorro II firmware) and a 68040/060 kernel with the card in
Zorro III — and every fast path stays reachable on either one.

| Option | Meaning |
|--------|---------|
| `-compat` | Turn every RTG fast path off and use the original code paths. First thing to try when something renders wrong. |
| `-bstore` | Enable backing store and save-unders. Off by default: one 800x600 window costs 960 KB, which matters on a 4–8 MB machine. |
| `-bus z2\|z3` | Override bus autodetection. Normally derived from the aperture size the driver reports. |
| `-blitmin N` | Use the CPU instead of the blitter for rectangles smaller than N pixels of area. `0` (default) always uses the blitter. |

Note that `-bs`, `-su` and `-wm` belong to the X server core and mean
something else; `-bstore` is the RTG option.

At startup Xrtg logs the configuration it detected. This is the first thing
to ask for in a bug report:

```
va2000: firmware 9, framebuffer 4032 KB, bus Zorro II
va2000: options 0x00af, blitmin 0, mapping 1984 KB
```

### Blitter timeouts

The wait for the blitter is bounded. An unbounded wait was how the known
`PolyFillRect` hang locked up the whole server, and recovering from that
needed a cold boot because it also wedged the AMIX screen manager. Xrtg now
gives up after a bound no real blit can reach, logs it, and continues:

```
va2000: blitter timeout, enable=0x1 (giving up)
va2000: 3 blitter timeouts this generation
```

A visual artefact is possible after a timeout; a repaint clears it. Any
non-zero count is worth reporting — it means the status register did not
clear when it should have.

### Session appearance

`session/Xdefaults` and `session/twmrc` are a starting point; copy them to
`/root/.Xdefaults` and `/root/.twmrc`. `startxrtg` loads the first with
`xrdb` and sets the root window and pointer before starting any client.

Two entries there are about speed rather than looks, and on this machine
they matter more than anything in the twmrc:

- `XTerm*jumpScroll: true` — redraw once after a burst of output instead of
  scrolling every line. Where a scroll is a blitter copy of the whole
  window, this is the difference between usable and not.
- `XClock*update: 60` — a seconds hand redraws the clock once a second,
  forever. Minutes are enough.
- No `OpaqueMove` in the twmrc: dragging a window opaquely copies it
  through VRAM at every step.

A tiled or image backdrop (`xv -root`, `xsetroot -bitmap`) needs the server
to paint window backgrounds from the pixmap rather than approximating it
with a single pixel. That is what the tile work fixed; against an older
server the backdrop goes flat the first time a window crosses it.

### Comparing two builds in a real session

`startxrtg` takes the server binary, mode and options from the environment,
so an A/B test is the same session with one thing changed rather than a
benchmark:

```sh
XSERVER=/usr/bin/X11/Xrtg.phase3b MODE=1280x720 startxrtg
XOPTS=-compat startxrtg
```

### XDM graphical login

Optional AMIX XDM integration provides a dark graphical login and selectable
sessions: **F1** uses the default desktop, **F2** starts amiwm, and **F3** starts
Open Look. **F4** opens a failsafe terminal; **F5** switches temporarily to the
native text console after confirmation. **Enter** uses the configured session
preference. The fresh-install default is twm; existing tvtwm customizations
can be retained. Optional desktop components must be installed separately.

Installation and boot activation are separate operations.  `install-xdm.sh`
backs up the existing configuration under `/root` and stages the files without
changing the boot mode. Local overrides and wallpaper stay outside version
control; no backdrop image is distributed. See [installation](docs/XDM.md),
[session selection](docs/XDM-SESSION-CHOOSER.md) and
[recovery](docs/XDM-RUNBOOK.md) for details. Review local customizations before
upgrading: the full installer replaces the shared configuration templates.

The patched AMIX XDM can be built in an isolated directory with
`build-xdm-amix.sh` and installed separately with `install-xdm-binary.sh`.
Both preserve the distinction between installation and boot activation.
See [recorded validation](docs/XDM-VALIDATION.md) for tested behavior and the
remaining interactive checks.

### Server diagnostics

Each binary logs to its own `/tmp/xrtg-<name>.log`, so the second run does
not overwrite the evidence from the first. The board line is printed at
startup and the counters are printed when the session ends:

```
va2000: firmware 49280, framebuffer 32704 KB, bus Zorro III
va2000: options 0x00af, blitmin 0, mapping 1984 KB
va2000: PutImage 9349 native, 1 via mi
va2000: glyph runs 9328 native, 0 via mi
va2000: blitter 0 timeouts, 3 slow waits, worst 118432 polls
```

A fast path that never fires and a fast path that does not help look the
same from outside; the counters tell them apart. The blitter line is the
one to watch if the desktop stops for a moment: a poll is roughly 200 ns,
so 118432 polls is about 24 ms of the server spinning.

### Measuring

`x11perf-baseline.sh` records a baseline against a running server. Commit
the result in `perf/` alongside the change it measures; see `perf/README.md`.

```sh
sh x11perf-baseline.sh /tmp/p.txt "phase0 z3 060"
```

### Checking a build before it reaches the machine

`tools/crosscheck.sh` runs on the build host, not on AMIX. It compiles the
RTG DDX with a m68k cross compiler against a mounted vanilla AMIX filesystem
and checks things a compile on the machine would not tell you — and one it
would, but only after a long build:

```sh
sh tools/crosscheck.sh -s /path/to/amix/rootfs
```

1. **Instructions the 68060 lacks.** A modern GCC emits the 64-bit-result
   forms of `MULS.L` and `DIVS.L` for `%` and for division by a literal
   constant. The 68060 does not implement them, so the kernel traps and
   emulates: the server still works, just slower, and nothing says so. A
   sample of eight `mi` and `dix` files contains 42 of them when built for a
   68030. `-m68020-60` produces none. Note that `-m68040` does *not* avoid
   them, although it is often offered as the compatible choice.
2. **Assembly the AMIX assembler will accept.** The cross toolchain and the
   machine agree on the compiler but not on the assembler, and GNU as is the
   permissive one — it accepts `move.l %sp,%fp` and quietly assembles it as
   `movea.l`, where the AMIX assembler refuses. The check generates assembly
   across a spread of the server and greps for mnemonics this target can
   never emit.
3. **Instructions neither processor shares.** Advisory only: `objdump -d` on
   a `.o` disassembles switch tables as code, so hits need a human look. The
   output says how to tell.

The sysroot is a mounted vanilla AMIX filesystem containing `usr/include`,
`usr/amiga/include` and `usr/x11r5`. Pass it with `-s` or set `AMIX_SYSROOT`;
no path to it is recorded in this repository. Exit status is 0 on pass, 1 on
a failed check, 2 on a usage error.

### Three compilers, and which one you are actually using

| | Compiler | Flags |
|---|---|---|
| Stock AMIX install | GNU C **1.40.5** (1991) | `amix.cf` with `AmixGccMajor 1` |
| Commonly installed | GNU C **2.7.2.3** under `/usr/local` | `amix.cf` with `AmixGccMajor 2` (default) |
| Cross build | a modern GCC | the `modern` profile in `tools/crosscheck.sh` |

Check which one you have:

```sh
gcc -v 2>&1 | head -1
```

This choice is not cosmetic. `-fcombine-regs`, which `amix.cf` passed to every
compilation for years, was **removed in GCC 2 and is fatal there** —
`cc1: Invalid option '-fcombine-regs'`, exit 1, no object. A tree configured
for 1.40 therefore cannot be built by 2.7.2.3 at all. If 2.7.2.3 is installed
and `AmixGccMajor` is left at 1, the build silently keeps using the 1991
compiler — whichever `gcc` your `PATH` finds first.

What each one can do, checked against the compilers themselves rather than
assumed:

- **1.40.5** has no `-O2` (`toplev.c` compares the switch with
  `strcmp(str, "O")`, and the driver spec passes `%{O}`), and
  `TARGET_SWITCHES` offers only `68000`, `68020`, `68881`, `bitfield`, `rtd`,
  `short`, `fpa` — with `TARGET_DEFAULT` already 68020 + 68881. It does have
  some of the individual passes GCC 2.x later folded into `-O2`, so `amix.cf`
  names those.
- **2.7.2.3** has `-O2`. It has no `-m68060` and no `-m68020-60`. It has
  `-m68020-40` and `-m68040`, but **do not use them here**: with either, this
  compiler replaces `link.w %fp,&0` with `pea (%fp)` + `move.l %sp,%fp`, and
  spells that one instruction MIT-style in a target whose syntax is `mov.l`.
  The AMIX assembler rejects it (`invalid instruction name`) and the build
  dies. `-m68030` does not take that path, and `-O2` was always the win.
- **A modern GCC** has `-m68020-60`, needs `-malign-int` for the AMIX ABI, and
  needs a build flow that links on the machine.

### The 68060 traps neither AMIX compiler can avoid

GCC generates the 64-bit-result forms of `MULS.L` and `DIVS.L` for `%` and for
division by a literal constant. The 68060 does not implement them; the kernel
traps and emulates, so the server works — just slower, with nothing saying so.

`crosscheck.sh` finds 31 of them in the DDX plus a sample of eleven `mi` and
`dix` files built with 2.7.2.3, and they are in real drawing code, not
initialisation: `miPolyBuildEdge` (wide lines), `miGetArcEdge` (filled arcs),
`miZeroArcSetup`, `miGetPlane`, `ProcPutImage`.

gcc 1.40.5 does *not* generate the reciprocal-multiply sequence, so a server
built by the stock compiler contains none — scanning the `X`, `twm` and
`xterm` binaries from a vanilla install finds zero. Nothing in 2.7.2.3 avoids
them. `-m68020-60` in a modern GCC does, which is the argument for the cross
toolchain.

---

## Architecture

```
X clients
    |
X server (Xrtg)
    |
amix DDX layer  (ddx/amix/)
    |
RTG interface   (ddx/amix/rtg/)   — device-independent RTG screen type
    |
VA2000 driver   (ddx/amix/rtg/va2000/)  — VA2000-specific hardware access
    |
/dev/va2000  (kernel driver — see va2000-amix)
    |
MNT VA2000 hardware
```

---

## Key Technical Notes

### RTG device selection fix

The original amix DDX initialises `amixFbs[n].type` to zero, which always
selects `amixMonoProbe` (the native display driver). In RTG builds the RTG
entry is at index 1. Without explicit pre-initialisation, the RTG probe is
never called regardless of compile-time flags.

Fix in `amixInit.c` `InitOutput()`:

```c
#ifdef RTG
    for (n = 0; n < MAXSCREENS; n++)
        amixFbs[n].type = RTG_INDEX;
#endif
```

### Input via AMIX screen manager

The AMIX screen manager (`scrmon`) routes keyboard and mouse events between
the native Amiga display and any RTG screens. `rtgProbe()` calls
`OpenScreen()` to register a screen context, then `SIOCACTIVATE` to make it
the active event target. The server reads raw keyboard and mouse events from
the resulting fd; `SIOCSETINPUTMODE(SIM_RAWKEY)` puts the keyboard in raw
mode.

`SIOCACTIVATE` routes events without switching the video output, so the
native Amiga screen is unaffected while Xrtg is running on the RTG card.

### Hardware blitter — PaintWindow

Window background fills (`PaintWindow`) use the VA2000 hardware blitter for
`BackgroundPixel` windows. The blitter addresses SDRAM directly; the correct
source address for row `y` is:

```c
src = (unsigned long)y * screen_width;  /* SDRAM word address, no fb_off */
SRC_HI = src >> 16;
SRC_LO = src & 0xffff;                  /* full 16 bits, no masking */
```

This formula was verified against `blit_test4.c` and the FPGA Verilog source.
An earlier version incorrectly added `VA2000_FB_WORDS` (0x8000) and masked
`SRC_LO` with `0xfc00`, which caused fills to write to the wrong SDRAM rows
and produced full-width horizontal stripes.

The blitter register sequence (fill mode):

```
BLT_SRC_HI  = src >> 16
BLT_SRC_LO  = src & 0xffff
BLT_ROWPITCH = screen_width
BLT_COLORMODE = 1             (16-bit)
BLT_RGB16   = fill colour
BLT_X1/Y1   = top-left (inclusive)
BLT_X2/Y2   = bottom-right (inclusive)
BLT_ENABLE  = 1              (fire; poll until 0)
```

### BuildPex cascade in X11R5

`Project.tmpl` defaults `BuildPex = YES`, cascading to `BuildPexClients = YES`
and causing `extensions/server/Makefile` to include `PEX/dipex/swap` which
requires `PEXproto.h` — absent on AMIX.

Fix: `#define BuildPex NO` in `amix.cf`.

### ResolveColor overflow (whitePixel = 0 bug)

`va2000ResolveColor` used `unsigned short` arithmetic where the intermediate
product `255 × 65535 = 16711425` overflows 16 bits, causing `whitePixel = 0`
and an invisible cursor.

Fix — two-step computation in `unsigned int`:

```c
idx   = ((unsigned int)*pRed * (limr + 1)) >> 16;
*pRed = (unsigned short)((idx * 65535) / limr);
```

### Framebuffer cleared on init

VRAM contains random content at power-on. `va2000InitHW` clears the
framebuffer to black with `memset` immediately after `mmap`.

### Server reset handling

`InitOutput()` uses `static int n = 0` so the screen count persists across
server generations (resets).

### TIGA symbol guard

`amixCursor.c` now guards `tigCursorInitialize()` with `#ifdef TIGA` to
avoid a link error in RTG-only builds.

---

## Disclaimer

This software is provided as-is. It was developed on real AMIX hardware
primarily out of curiosity. It has no warranty, no support commitment, and
probably no sane reason to exist. Use it at your own risk, preferably on
hardware you can afford to break.

---

## License

New code and modifications: MIT License — see LICENSE file.

The X11R5 source files retain their original X Consortium copyright and
MIT/X11 license terms. The AMIX DDX framework carries its original Regents
of the University of California and Sun Microsystems copyright.

See individual source file headers for details.

-Antti Sokero 2026
