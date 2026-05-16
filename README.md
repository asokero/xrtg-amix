# Xrtg — X11R5 RTG Server for Amiga UNIX (AMIX)

## Overview

Xrtg is an X11R5 server for Amiga UNIX (AMIX) SVR4 that drives the MNT VA2000
RTG graphics card directly — bypassing the native Amiga display chipset.

This is a companion project to
[va2000-amix](https://github.com/asokero/va2000-amix), which provides the
kernel device driver for the VA2000. Xrtg builds on top of that driver to run
a full X11 session in 16-bit color via the VA2000 framebuffer.

Xrtg is built entirely from the vanilla AMIX X11R5 source tree (Keith
Gabryelski's public domain port of X11R5 to AMIX), with a new RTG DDX layer
and VA2000-specific driver added on top. No external packages were used.

Klaus Burkert's Xsvga (from Gateway! Volume 2) is a parallel project doing
the same thing for Cirrus Logic-based RTG cards. Xsvga demonstrated that
building an RTG X11 server on vanilla AMIX is possible and served as
inspiration for this project.

This is a vibe-coding adventure in a very unreasonable direction. The goal is
to explore how far a modern RTG card can be pushed under a 1991 Unix on
35-year-old hardware.

---

## Status (May 2026)

**Tested on:** Amiga 3000, 68030, AMIX SVR4 2.1p2a, MNT VA2000 fw1.9.0b2

### Working

- **Server compiles and links** on AMIX SVR4 — confirmed
- **X server starts** — passes font path, initialises screen, accepts connections
- **Build infrastructure** — imake config, RTG Imakefiles, install scripts

### In Progress

- **VA2000 RTG activation** — the fix that forces the RTG device path at probe
  time (`amixInit.c` RTG_INDEX pre-initialisation) is implemented. A rebuild
  on AMIX hardware is needed to confirm the VA2000 display activates on X start.

### Known Issues

- Until the RTG activation fix is confirmed, X starts in Amiga native passthrough
  mode instead of switching to the VA2000 framebuffer.

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
install-sources.sh               Install modified sources into /usr/x11r5 tree
install-xrtg.sh                  Install compiled Xrtg binary to /usr/bin/X11

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

    ddx/amix/rtg/
      Imakefile                  RTG layer Imakefile
      rtg.h                      RTG type definitions
      rtgInit.c                  rtgProbe / rtgCreate — screen init entry points

    ddx/amix/rtg/va2000/
      Imakefile                  VA2000 driver Imakefile
      va2000.h                   Hardware definitions, function declarations
      va2000hw.c                 Hardware init — mode set, register programming
      va2000screen.c             Screen setup — depth, colormap, GC operations
      va2000draw.c               Drawing operations — spans, fill, copy
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

1. AMIX SVR4 2.1p2a installed with the vanilla X11R5 source tree at `/usr/x11r5`
2. VA2000 kernel driver installed (`/dev/va2000` exists, returns data)
3. Transfer this repository to the AMIX machine

### 1. Install sources into X11R5 tree

Run from `/usr/x11r5`:

```sh
cd /usr/x11r5
sh /path/to/install-sources.sh /path/to/xrtg-amix
```

The script:
- Backs up modified files (`.orig` suffix)
- Copies new RTG source files into place
- Copies pre-built `libXau.a` and `libXdmcp.a` from `/usr/X/lib/`
- Creates the `X11 -> include` symlink for makedepend
- Regenerates `extensions/server/Makefile` and `server/Makefile` via imake
- Checks for `libfont.a` and `libext.a` and warns if missing

### 2. Build prerequisite libraries

These must be built before the server link step:

```sh
cd /usr/x11r5/fonts/lib/font && make
cd /usr/x11r5/extensions/server && make
```

### 3. Build Xrtg

```sh
cd /usr/x11r5/server
make Makefiles && make depend && make Xrtg
```

The binary lands at `/usr/x11r5/server/Xrtg`.

### 4. Install the binary

Run as root from `/usr/x11r5/server`:

```sh
sh /path/to/install-xrtg.sh
```

Installs `Xrtg` to `/usr/bin/X11/Xrtg` and creates the `X -> Xrtg` symlink.

### 5. Start X

```sh
xinit -- /usr/bin/X11/Xrtg
```

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

### BuildPex cascade in X11R5

`Project.tmpl` defaults `BuildPex = YES`, which cascades to
`BuildPexClients = YES` and causes `extensions/server/Makefile` to include
`PEX/dipex/swap` in `SUBDIRS`. PEXproto.h does not exist on AMIX.

Fix: `#define BuildPex NO` in `amix.cf` (not just `BuildPexExt NO`).
The `install-sources.sh` also regenerates `extensions/server/Makefile` via
imake to pick up this setting.

### TIGA symbol guard

`amixCursor.c` calls `tigCursorInitialize()` unconditionally. TIGA is not
compiled in the RTG build, so this symbol is undefined at link time.

Fix: `#ifdef TIGA` guard around the call. Without TIGA, `amixCursorInitialize`
returns `FALSE` and `amixScreenInit` falls back to `miDCInitialize`.

### ForceSubdirs does not build libraries

`ForceSubdirs` in X11R5 imake generates directory targets, not `.a` file
targets. `libfont.a` and `libext.a` must be built by entering their directories
explicitly before `make Xrtg`.

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
