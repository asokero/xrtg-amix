# Phase 3: native glyph drawing

Same machine and setup as the other notes in this directory.

## Result

| | chars/sec | note |
|---|---:|---|
| before (miPolyGlyphBlt) | 3250 - 5254 | many runs, several builds |
| `text` after | 12825 - 24034 | |
| `imagetext` after | 7369 - 13748 | |

So **2.5x to 4.6x on PolyText, 1.4x to 2.7x on ImageText**. The spread is
the machine's own; the point is that the whole after-range sits above the
whole before-range on PolyText.

Per character: 190-290 us before, 42-78 us after.

The counters say the fast path is not merely available but universal in
this workload:

    va2000: glyph runs 3131 native, 0 via mi
    va2000: glyph runs 9328 native, 0 via mi

Nothing fell back. The clip test -- text must land inside a single clip
rectangle -- never failed once, which is what you would expect for a
benchmark drawing into its own unobscured window, and is roughly what a
terminal does too.

## Less than the review guessed, and the reason is interesting

The review estimated 10-30x from the operation counts: miPushPixels walks
32 bits per word per scanline and allocates a pixmap and a scratch GC per
call, against a byte-load and eight stores.

Getting 2.5-4.6x instead means the glyph loop was never the whole cost.
What remains is the same thing phase 2 found under PutImage: the protocol
and dispatch around the drawing. A `PolyText8` request carries its string
through the socket, the dispatcher decodes it, `ValidateGC` runs, the font
is looked up per character. At 24000 chars/sec the server is spending
about 42 us per character, and the inner loop for a 8x13 glyph is perhaps
100 stores -- nowhere near that.

Which says the next thing worth measuring on this path is not a faster
inner loop but how much of the 42 us is dispatch. `gcchange` measures part
of it already.

## What is not covered

- Text that straddles a clip boundary still goes to mi. A terminal only
  hits that when another window overlaps it.
- `FillStippled` and `FillTiled` GCs go to mi: the fast path requires
  FillSolid. Phase 7 changes that.
- The 1-bit path (cursor, bitmaps) is untouched and still mfb's.
