# Troubleshooting — Xrtg Build and Installation on AMIX

Known problems encountered when building and installing Xrtg on AMIX SVR4.

---

## 1. `don't know how to make /usr/x11r5/lib/Xau/libXau.a`

**Problem:** The server link step fails because `libXau.a` (or `libXdmcp.a`)
does not exist at the path imake expects.

**Cause:** imake sets `XAUTHSRC = $(TOP)/lib/Xau` and `XDMCPLIBSRC = $(TOP)/lib/Xdmcp`.
The source directories exist under `/usr/x11r5/lib/` but no pre-built `.a`
files are there. The running X installation keeps pre-compiled m68k versions
at `/usr/X/lib/libXau.a` and `/usr/X/lib/libXdmcp.a`.

**Solution:** `install-sources.sh` copies these directly. If you installed
manually without the script, copy them by hand:

```sh
mkdir -p /usr/x11r5/lib/Xau /usr/x11r5/lib/Xdmcp
cp /usr/X/lib/libXau.a   /usr/x11r5/lib/Xau/libXau.a
cp /usr/X/lib/libXdmcp.a /usr/x11r5/lib/Xdmcp/libXdmcp.a
```

---

## 2. `don't know how to make fonts/lib/font/libfont.a`

**Problem:** The server link step fails because `libfont.a` does not exist.

**Cause:** `ForceSubdirs` in X11R5 imake generates directory-level `make`
targets, not file-level targets. The top-level server `Makefile` cannot
directly build `libfont.a` — it only enters the subdirectory. The `.a` file
must pre-exist before the server link runs.

**Solution:** Build `libfont.a` explicitly before building the server:

```sh
cd /usr/x11r5/fonts/lib/font && make
```

Similarly, `extensions/server/libext.a` must also be pre-built:

```sh
cd /usr/x11r5/extensions/server && make
```

Only then run the server build:

```sh
cd /usr/x11r5/server && make Makefiles && make depend && make Xrtg
```

---

## 3. `extensions/server make: PEXproto.h: No such file or directory`

**Problem:** Building `extensions/server` fails immediately because the
`Makefile` tries to enter `PEX/dipex/swap` which includes `PEXproto.h`,
a file that does not exist on AMIX.

**Cause:** X11R5's `Project.tmpl` defaults `BuildPex = YES`, which cascades
to `BuildPexClients = YES`. This causes imake to set `PEXDIRS = PEX/dipex/swap`
in `extensions/server/Makefile`. The vanilla `Makefile` was generated with
this setting. Setting only `BuildPexExt NO` in `amix.cf` is not sufficient —
it removes PEX from the server extension list but does not affect
`BuildPexClients` which controls the `extensions/server` subdirectory list.

**Solution:** Add `#define BuildPex NO` to `amix.cf` (not just
`BuildPexExt NO`), then regenerate `extensions/server/Makefile` via imake:

```sh
cd /usr/x11r5/extensions/server
/usr/x11r5/config/imake -I../../config -DTOPDIR=/usr/x11r5 -DCURDIR=/usr/x11r5/extensions/server
```

`install-sources.sh` does this automatically. If you regenerated
`extensions/server/Makefile` without first adding `BuildPex NO` to `amix.cf`,
repeat the sequence: update `amix.cf`, then regenerate the Makefile.

To verify the fix worked:

```sh
grep PEXDIRS /usr/x11r5/extensions/server/Makefile
```

Expected: the line should be empty (`PEXDIRS =`) or absent.

---

## 4. `ld: Undefined symbol _miGetSpans` in `libva2000.a`

**Problem:** The server link fails because `miGetSpans` is undefined.

**Cause:** `miGetSpans` does not exist as an exported symbol in the X11R5 mi
library. `GetSpans` is a function pointer in the screen structure, not a
globally callable function. An earlier version of `va2000screen.c` referenced
`miGetSpans` by name and assigned it to `pScreen->GetSpans`.

**Solution:** Use the VA2000-specific implementation `va2000GetSpans` (in
`va2000draw.c`) instead. The current `va2000screen.c` already does this:

```c
pScreen->GetSpans = va2000GetSpans;
```

If you have an older version of `va2000screen.c`, re-run `install-sources.sh`
to get the corrected file.

---

## 5. `ld: Undefined symbol _tigCursorInitialize`

**Problem:** The server link fails because `tigCursorInitialize` is undefined.

**Cause:** The original `amixCursor.c` calls `tigCursorInitialize()` without
any conditional compilation guard. TIGA is a separate display subsystem not
present in RTG builds. When `-DTIGA` is not passed (which it is not in RTG
builds), the TIGA library is not linked, so the symbol is never defined.

**Solution:** The call must be guarded with `#ifdef TIGA`. The current
`amixCursor.c` already contains this fix:

```c
Bool
amixCursorInitialize(pScreen)
    ScreenPtr pScreen;
{
    SetupScreen(pScreen);
#ifdef TIGA
    if (amixFbs[pScreen->myNum].type == DMI_RESOLVER)
    {
        tigCursorInitialize(pScreen);
        return TRUE;
    }
#endif /* TIGA */
    return FALSE;
}
```

Without TIGA, `amixCursorInitialize` returns `FALSE` and `amixScreenInit`
falls back to `miDCInitialize` for cursor handling.

Re-run `install-sources.sh` to install the corrected file if you have an
older version.

---

## 6. `fatal server error: could not open default font 'fixed'`

**Problem:** Xrtg starts but immediately exits with a fatal error about the
'fixed' font.

**Cause:** The X11R5 server's compiled-in default font path includes a
`Speedo/` directory entry. X11R5 rejects the **entire** font path if any
single component directory does not exist. `Speedo/` was not shipped with
the AMIX X installation. Because the whole path is rejected, even `misc/`
(which contains the `fixed` font) becomes unreachable.

**Solution:** Create an empty `Speedo/` directory with a valid `fonts.dir`
file:

```sh
mkdir /usr/X/lib/fonts/Speedo
echo "0" > /usr/X/lib/fonts/Speedo/fonts.dir
```

`install-sources.sh` and `install-xrtg.sh` both do this automatically. If
the error persists after installation, verify the directory and file exist:

```sh
ls -la /usr/X/lib/fonts/Speedo/fonts.dir
```

As a fallback, pass the font path explicitly on the command line:

```sh
xinit -- /usr/bin/X11/Xrtg -fp /usr/X/lib/fonts/misc/,/usr/X/lib/fonts/75dpi/,/usr/X/lib/fonts/100dpi/
```

---

## 7. X starts in Amiga native mode instead of activating the VA2000

**Problem:** X starts without errors, but the display output comes from the
Amiga's native chipset instead of the VA2000 RTG card.

**Cause:** `amixFbs[n].type` is a global array that is zero-initialised by
the C runtime. Zero selects `amixFbData[0]`, which is `amixMonoProbe` — the
native Amiga monochrome display probe. In RTG builds the RTG entry is at
index 1. The probe loop in `InitOutput()` therefore always calls
`amixMonoProbe` regardless of the `-DRTG -DVA2000` compile flags.

**Solution:** Pre-initialise `amixFbs[n].type` to `RTG_INDEX` before the
probe loop. This is done in `amixInit.c`:

```c
#ifdef RTG
#ifdef TIGA
#define RTG_INDEX 2
#else
#define RTG_INDEX 1
#endif
    for (n = 0; n < MAXSCREENS; n++)
        amixFbs[n].type = RTG_INDEX;
#undef RTG_INDEX
#endif /* RTG */
```

The current `amixInit.c` contains this fix. Re-run `install-sources.sh`
and rebuild the server to pick it up:

```sh
cd /usr/x11r5/server && make Xrtg
```

Then install and restart:

```sh
sh /path/to/install-xrtg.sh
xinit -- /usr/bin/X11/Xrtg
```

---

## 8. `make: fatal error: don't know how to make X.h (bu42)`

**Problem:** `make depend` fails because makedepend cannot find `X.h`.

**Cause:** imake sets `INCLUDESRC = $(TOP)/X11 = /usr/x11r5/X11` in every
generated `Makefile`. On AMIX, X11 headers live in `/usr/x11r5/include/`
without an `X11/` subdirectory. makedepend is invoked with
`-I/usr/x11r5/X11` which does not resolve, so it writes bare filenames
like `X.h` into dependency files. `make` then tries to build `X.h` as a
target and fails.

**Solution:** Create a symlink so the expected path resolves:

```sh
cd /usr/x11r5
ln -s include X11
```

`install-sources.sh` creates this symlink automatically. If the error
persists, verify the symlink:

```sh
ls -la /usr/x11r5/X11
```

Expected: `X11 -> include`

---

## 9. Build fails referencing `Xdmcp.h` with `don't know how to make` error

**Problem:** `make depend` writes bare `Xdmcp.h` into dependency files,
causing the same `bu42` build failure as in issue 8, but for `Xdmcp.h`.

**Cause:** `os/xdmcp.c` includes `"Xdmcp.h"`. The header lives at
`lib/Xdmcp/Xdmcp.h` but the `os/` Imakefile does not add
`-I$(TOP)/lib/Xdmcp` to `INCLUDES`. makedepend cannot find it via
`-I/usr/x11r5/X11` alone and writes a bare filename.

**Solution:** Symlink `Xdmcp.h` into the include directory:

```sh
cd /usr/x11r5
ln -s ../lib/Xdmcp/Xdmcp.h include/Xdmcp.h
```

`install-sources.sh` creates this symlink automatically.
