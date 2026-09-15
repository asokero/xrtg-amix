/*
** va2000tile.c — tile and stipple fills for VA2000 RTG, depth 16.
**
** WHAT THIS REPLACES.  ValidateGC sent FillTiled, FillStippled and
** FillOpaqueStippled to the solid span filler, so every one of them painted
** a flat block of the foreground colour.  That is not a cosmetic shortfall:
**
**   - A root window with a background pixmap -- which is what `xv -root` and
**     `xsetroot -bitmap` set -- came back as one colour the first time
**     anything exposed it.  So a backdrop survived until you moved a window
**     over it.
**   - OPEN LOOK draws its whole three-dimensional look with 50% grey
**     stipples.  XView would have compiled and started and looked broken.
**
** So this file is the prerequisite for two of the three things the review
** set out to do, and it is a correctness fix rather than a speedup: a real
** tile costs more than a flat fill, and is what was asked for.
**
** ORIGIN.  The pattern is anchored at pDrawable->x + pGC->patOrg.x, and the
** spans arrive in screen coordinates because miTranslate is on.  cfb does
** the same arithmetic in cfbfillsp.c; getting it wrong shifts the pattern
** by a window position, which looks like a tiling bug and is not.
**
** BIT ORDER.  Stipples are depth-1 pixmaps, MSBFirst on AMIX, rows padded
** to 32 bits -- devKind is the row length in bytes.
*/

#include <string.h>
#include "X.h"
#include "Xmd.h"
#include "scrnintstr.h"
#include "regionstr.h"
#include "windowstr.h"
#include "pixmapstr.h"
#include "gcstruct.h"
#include "servermd.h"
#include "mi.h"
#include "../../amix.h"
#include "../rtg.h"
#include "va2000.h"

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

extern void va2000FillSpans();

/*
** modulus that gives a non-negative answer.  The pattern origin can be to
** the right of or below the span, and C's % keeps the sign of the dividend.
*/
#define WRAP(v, m)  (((v) % (m) + (m)) % (m))

/* ------------------------------------------------------------------ */

/*
** Scratch row, wide enough for the widest mode this driver offers.
**
** One expanded copy of the pattern, built in host memory and written to the
** target in a single long run.  Sized once and reused; no allocation on a
** drawing path.
*/
/*
** Expanded rows, kept between calls.
**
** Expanding the pattern once per output row was already far better than
** copying the tile hundreds of times per row, but it is still redundant: a
** tile repeats vertically with period th, so a full-screen fill builds the
** same four or eight rows a hundred and eighty times each.
**
** Measured before this cache, painting a 1280x720 tiled root cost 625 ms, of
** which the arithmetic says about 143 ms was expansion -- 1280 pixels of
** host-memory writes per row at the machine's measured 0.155 us/px, 720
** times.  The copy to VRAM underneath it is 427 ms and cannot be avoided.
**
** So keep the expanded rows.  The key is everything the content depends on:
** which tile, its geometry, which row of it, and the horizontal phase, since
** a span that does not start on a tile boundary is a rotation of one that
** does.  Keying on all of it means no invalidation logic and no way for a
** stale row to be used -- a miss just expands as before.
*/
#define TILE_SCRATCH 1920
#define TILE_CACHE   8

typedef struct {
    unsigned short *tbase;      /* which tile        */
    int             tw, th;     /* its geometry      */
    int             row;        /* which row of it   */
    int             phase;      /* horizontal offset */
    int             width;      /* how much is built */
    unsigned short  px[TILE_SCRATCH];
} TileRow;

static TileRow tileCache[TILE_CACHE];
static int     tileCacheNext = 0;

long va2000_tile_hit  = 0;
long va2000_tile_miss = 0;

/*
** Which path the tiled fills took, and how much they moved.
**
** The pixel counters alone cannot answer the question they were built for.
** A stretch that reports 922624 pixels copied in 3667 ms looks like "the
** driver was barely involved" if you assume the pixels cost what the
** hardware says they should -- but slow driver work produces exactly the
** same line, because these count pixels and not time.  Counting which path
** each row took separates the two: the expanded path is a handful of long
** writes, the short loop is hundreds of tiny ones, and knowing which ran is
** knowing whether the time was spent here.
*/
long va2000_tile_px     = 0;
long va2000_tile_wide   = 0;
long va2000_tile_narrow = 0;

/*
** tileRow -- write one row of a tiled fill.
**
** The obvious loop copies tw pixels at a time straight to the target, and it
** is far too slow to use.  X's default root background is a 4x4 weave, so a
** 1280-pixel row is 320 calls of four pixels each, every one of them below
** the eight-pixel floor where va2000CopyRun stops bothering with longword
** writes.  Measured on the machine: 7.9 seconds of solid CPU for the two
** full-screen paints the server does at startup, with the blitter idle
** throughout.  That is the desktop freeze, and it happens on any full-screen
** expose of a tiled root.
**
** So expand the pattern once into a host-memory scratch row -- doubling, so
** it costs log2(w/tw) passes rather than w/tw -- and then write the whole
** span to the target in one call.  Host memory is where the cheap writes
** are; the target is VRAM, and it should be touched in long runs only.
**
** Below the doubling threshold the old loop is still the right shape: a
** handful of short runs is not worth building a buffer for.
*/
static void
tileRow(dst, y, x1, w, tbase, tstride, tw, th, xrot, yrot)
unsigned short *dst;            /* first pixel of the span, in the target  */
int             y, x1, w;       /* span position and length, screen coords */
unsigned short *tbase;
int             tstride;        /* tile row length in pixels               */
int             tw, th;
int             xrot, yrot;     /* where the pattern is anchored           */
{
    unsigned short *trow;
    int             tx, run, have, take;

    trow = tbase + WRAP(y - yrot, th) * tstride;
    tx   = WRAP(x1 - xrot, tw);

    va2000_tile_px += w;

    if (w >= 32 && w <= TILE_SCRATCH && tw < w)
    {
        TileRow *tr;
        int      i, rowidx;

        va2000_tile_wide++;
        rowidx = WRAP(y - yrot, th);

        for (i = 0; i < TILE_CACHE; i++)
        {
            tr = &tileCache[i];
            if (tr->tbase == tbase && tr->tw == tw && tr->th == th &&
                tr->row == rowidx && tr->phase == tx && tr->width >= w)
            {
                va2000_tile_hit++;
                va2000CopyRun(dst, tr->px, w);
                return;
            }
        }

        va2000_tile_miss++;
        tr = &tileCache[tileCacheNext];
        tileCacheNext = (tileCacheNext + 1) % TILE_CACHE;

        /* First copy: the pattern from its current phase, once. */
        have = tw - tx;
        if (have > w)
            have = w;
        for (run = 0; run < have; run++)
            tr->px[run] = trow[tx + run];

        /* Then double what we have until the row is full. */
        while (have < w)
        {
            take = have;
            if (take > w - have)
                take = w - have;
            for (run = 0; run < take; run++)
                tr->px[have + run] = tr->px[run];
            have += take;
        }

        tr->tbase = tbase;
        tr->tw    = tw;
        tr->th    = th;
        tr->row   = rowidx;
        tr->phase = tx;
        tr->width = w;

        va2000CopyRun(dst, tr->px, w);
        return;
    }

    va2000_tile_narrow++;

    while (w > 0)
    {
        run = tw - tx;
        if (run > w)
            run = w;

        va2000CopyRun(dst, trow + tx, run);

        dst += run;
        w   -= run;
        tx  += run;
        if (tx >= tw)
            tx = 0;
    }
}

/*
** stipRow — one span of a stippled fill.
**
** opaque selects between FillStippled, which leaves the zero bits alone,
** and FillOpaqueStippled, which paints them in the background colour.
*/
static void
stipRow(dst, y, x1, w, sbits, sstride, sw, sh, xrot, yrot, fg, bg, opaque)
unsigned short *dst;
int             y, x1, w;
unsigned char  *sbits;
int             sstride;        /* stipple row length in BYTES             */
int             sw, sh;
int             xrot, yrot;
unsigned short  fg, bg;
int             opaque;
{
    unsigned char *srow;
    int            sx;

    srow = sbits + WRAP(y - yrot, sh) * sstride;
    sx   = WRAP(x1 - xrot, sw);

    while (w-- > 0)
    {
        if (srow[sx >> 3] & (0x80 >> (sx & 7)))
            *dst = fg;
        else if (opaque)
            *dst = bg;
        dst++;
        if (++sx >= sw)
            sx = 0;
    }
}

/* ------------------------------------------------------------------ */

/*
** usableFill — the conditions both fillers need.
**
** When they do not hold the call goes to the solid filler, which is what
** happened for every such call before this file existed.  The cases are
** rare -- a non-copy alu or a partial planemask on a patterned fill -- and
** no worse off than they were.
*/
static int
usableFill(pDraw, pGC)
DrawablePtr pDraw;
GCPtr       pGC;
{
    return (VA2000_OPT(VA2000_OPT_TILE)   &&
            pDraw->depth == VA2000_DEPTH  &&
            pGC->alu == GXcopy            &&
            (pGC->planemask & 0xffffUL) == 0xffffUL);
}

/*
** va2000FillTileSpans — FillTiled span fill.
*/
void
va2000FillTileSpans(pDraw, pGC, nspans, ppts, pwidths, fSorted)
DrawablePtr pDraw;
GCPtr       pGC;
int         nspans;
DDXPointPtr ppts;
int        *pwidths;
int         fSorted;
{
    rtgScreenPtr    pRTG = GetRTGScreen(pDraw->pScreen);
    PixmapPtr       pTile = pGC->tile.pixmap;
    unsigned short *base;
    unsigned short *tbase;
    int             stride, tstride, tw, th;
    int             xrot, yrot;
    int             i, w;

    if (!usableFill(pDraw, pGC) || !pTile ||
        pTile->drawable.depth != VA2000_DEPTH)
    {
        va2000FillSpans(pDraw, pGC, nspans, ppts, pwidths, fSorted);
        return;
    }

    base    = (pDraw->type == DRAWABLE_WINDOW)
              ? pRTG->fbBase
              : (unsigned short *) ((PixmapPtr) pDraw)->devPrivate.ptr;
    stride  = (pDraw->type == DRAWABLE_WINDOW)
              ? va2000_selected_mode->w
              : (int) ((PixmapPtr) pDraw)->devKind / 2;

    tbase   = (unsigned short *) pTile->devPrivate.ptr;
    tstride = (int) pTile->devKind / 2;
    tw      = (int) pTile->drawable.width;
    th      = (int) pTile->drawable.height;
    if (tw <= 0 || th <= 0)
        return;

    xrot = pDraw->x + pGC->patOrg.x;
    yrot = pDraw->y + pGC->patOrg.y;

    nspans = miClipSpans(rtgGCClip(pGC), ppts, pwidths, nspans,
                         ppts, pwidths, fSorted);

    for (i = 0; i < nspans; i++)
    {
        w = pwidths[i];
        if (w <= 0)
            continue;
        tileRow(base + ppts[i].y * stride + ppts[i].x,
                ppts[i].y, ppts[i].x, w,
                tbase, tstride, tw, th, xrot, yrot);
    }
}

/*
** va2000FillStipSpans — FillStippled and FillOpaqueStippled span fill.
*/
void
va2000FillStipSpans(pDraw, pGC, nspans, ppts, pwidths, fSorted)
DrawablePtr pDraw;
GCPtr       pGC;
int         nspans;
DDXPointPtr ppts;
int        *pwidths;
int         fSorted;
{
    rtgScreenPtr    pRTG = GetRTGScreen(pDraw->pScreen);
    PixmapPtr       pStip = pGC->stipple;
    unsigned short *base;
    unsigned char  *sbits;
    int             stride, sstride, sw, sh;
    int             xrot, yrot, opaque;
    int             i, w;

    if (!usableFill(pDraw, pGC) || !pStip ||
        pStip->drawable.depth != 1)
    {
        va2000FillSpans(pDraw, pGC, nspans, ppts, pwidths, fSorted);
        return;
    }

    base    = (pDraw->type == DRAWABLE_WINDOW)
              ? pRTG->fbBase
              : (unsigned short *) ((PixmapPtr) pDraw)->devPrivate.ptr;
    stride  = (pDraw->type == DRAWABLE_WINDOW)
              ? va2000_selected_mode->w
              : (int) ((PixmapPtr) pDraw)->devKind / 2;

    sbits   = (unsigned char *) pStip->devPrivate.ptr;
    sstride = (int) pStip->devKind;
    sw      = (int) pStip->drawable.width;
    sh      = (int) pStip->drawable.height;
    if (sw <= 0 || sh <= 0)
        return;

    opaque = (pGC->fillStyle == FillOpaqueStippled);
    xrot   = pDraw->x + pGC->patOrg.x;
    yrot   = pDraw->y + pGC->patOrg.y;

    nspans = miClipSpans(rtgGCClip(pGC), ppts, pwidths, nspans,
                         ppts, pwidths, fSorted);

    for (i = 0; i < nspans; i++)
    {
        w = pwidths[i];
        if (w <= 0)
            continue;
        stipRow(base + ppts[i].y * stride + ppts[i].x,
                ppts[i].y, ppts[i].x, w,
                sbits, sstride, sw, sh, xrot, yrot,
                (unsigned short) pGC->fgPixel,
                (unsigned short) pGC->bgPixel,
                opaque);
    }
}

/*
** va2000TileBoxes — paint a region with a tile, for PaintWindow.
**
** No GC here: window backgrounds and borders are painted without one, and
** the region is already the exact area to cover.  The pattern is anchored
** at the window origin, which is what X specifies for both.
*/
void
va2000TileBoxes(pDraw, prgn, pTile, xrot, yrot)
DrawablePtr pDraw;
RegionPtr   prgn;
PixmapPtr   pTile;
int         xrot, yrot;
{
    rtgScreenPtr    pRTG = GetRTGScreen(pDraw->pScreen);
    unsigned short *base;
    unsigned short *tbase;
    int             stride, tstride, tw, th;
    BoxPtr          pbox;
    int             nbox, y;

    if (!pTile || pTile->drawable.depth != VA2000_DEPTH)
        return;

    base    = (pDraw->type == DRAWABLE_WINDOW)
              ? pRTG->fbBase
              : (unsigned short *) ((PixmapPtr) pDraw)->devPrivate.ptr;
    stride  = (pDraw->type == DRAWABLE_WINDOW)
              ? va2000_selected_mode->w
              : (int) ((PixmapPtr) pDraw)->devKind / 2;

    tbase   = (unsigned short *) pTile->devPrivate.ptr;
    tstride = (int) pTile->devKind / 2;
    tw      = (int) pTile->drawable.width;
    th      = (int) pTile->drawable.height;
    if (tw <= 0 || th <= 0)
        return;

    nbox = REGION_NUM_RECTS(prgn);
    pbox = REGION_RECTS(prgn);

    while (nbox--)
    {
        for (y = pbox->y1; y < pbox->y2; y++)
            tileRow(base + y * stride + pbox->x1,
                    y, pbox->x1, pbox->x2 - pbox->x1,
                    tbase, tstride, tw, th, xrot, yrot);
        pbox++;
    }
}
