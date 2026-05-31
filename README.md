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

**Tested on:** Amiga 3000, 68030, AMIX SVR4 2.1p2a, MNT VA2000 fw1.9.0b2

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

- Amiga with Zorro II slots (tested on Amiga 3000, 68030)
- MNT VA2000 graphics card
- Amiga UNIX (AMIX) System V Release 4.0, version 2.1p2a
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
