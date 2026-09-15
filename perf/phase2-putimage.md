# Phase 2: the native PutImage path works, and PutImage is transport-bound

Same machine and setup as `phase0-1-z3-060.md`.

## The fast path is taken

The server counts which branch each PutImage call goes down. One xbench
run, 150 XPutImage calls:

    va2000: PutImage 150 native, 1 via mi

The one via mi is the software cursor's 1-bit image. So the native path
applies to everything a client pushing pixels sends, exactly as intended.

## And it changes nothing measurable

| build | putimage101x100 |
|-------|----------------:|
| phase 1 (miPutImage) | 25.0 / 33.3 / 33.3 |
| phase 2 (native) | 18.3 / 33.6 / 33.6 |
| phase 2 with `-compat` | 33.3 / 33.3 / 19.4 |

Identical, and the spread inside each column is the machine's own.

## Why: three quarters of the cost is moving the bytes, not drawing them

Two image sizes separate the per-request cost from the per-byte cost:

| test | ops/sec | pixels | Mpixel/s |
|------|--------:|-------:|---------:|
| putimage101x100 | 32.1 | 10100 | 0.324 |
| putimage11x10 | 2015.2 | 110 | 0.222 |

Fitting `time = a + b*pixels`:

    b = 3.07 us per pixel
    a = 0.16 ms per request

So the cost is per pixel, not per request. Of those 3.07 us, `va2000_bench`
says a 16-bit CPU write to this board costs about 0.79 us (2.55 MB/s), which
leaves **about 2.3 us per pixel -- roughly three quarters -- in the
transport**: the client's socket write, the server's socket read, and the
buffer copies on both sides. That works out to 0.88 MB/s through the
connection, which is unremarkable for a 1992 SVR4 stack.

Replacing the drawing half of a path that is three-quarters transport
cannot show up, and did not.

## What this means for the saku26 demo

The demo attributed 170-200 ms of its 252 ms frame to "mi + SetSpans
machinery" after subtracting an *estimated* 20 ms of socket copy. That
estimate looks about ten times too low: at 0.88 MB/s, a 460 KB frame is
roughly half a second of transport alone, and the demo's 252 ms for a
640x360 RGB565 frame (460 KB) is right at that limit.

**MIT-SHM is the lever there, not the DDX** -- and it is already compiled
into this server; `xdpyinfo` lists it among four extensions. A client that
puts its pixels in shared memory skips the socket copy entirely, which is
the three quarters.

## The other half of phase 2 is not measurable here

The 32-bit store change is in `fillRun`, which serves `FillSpans` and
pixmap fills. It does *not* serve the `fillrect` tests: those go to a
window, so `va2000SolidRect` sends them to the blitter and `fillRun` is
never called. The tests that would show it -- lines, polygon fills, the mi
text path -- are among the noisiest here. It stays in on the strength of
`va2000_bench`'s 4.87 vs 2.55 MB/s, not on a measurement of this server.
