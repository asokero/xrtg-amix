/*
** va2000text.c — native glyph drawing for VA2000 RTG, depth 16.
**
** WHAT THIS REPLACES.  Without it the server draws every character through
** miPolyGlyphBlt, which is the machine-independent last resort and behaves
** like one.  Per PolyText8 call it creates a 1-bit pixmap the size of the
** font's largest glyph and borrows a scratch GC; then, per character, it
** copies the glyph into that pixmap with a full PutImage and squeezes it
** onto the screen with miPushPixels -- which reads the pixmap back one
** scanline at a time with GetSpans and walks all 32 bits of every word
** deciding where the spans are.  Measured on this machine that is 190-290
** microseconds per character.
**
** What is actually required is: read a byte of glyph bits, write up to
** eight pixels.  That is what this file does.
**
** SCOPE.  Only the case that carries a desktop is handled here -- depth 16,
** GXcopy, FillSolid, a full planemask, and text that lands inside one clip
** rectangle.  Everything else falls back to mi, which is correct for all of
** it and rare in practice: the clip test fails only when a window edge or
** another window cuts through the middle of the string, and then one call
** goes the slow way.
**
** BIT ORDER.  servermd.h gives AMIX BITMAP_BIT_ORDER MSBFirst and
** GLYPHPADBYTES 4, so the leftmost pixel of a row is bit 7 of the first
** byte and each row starts on a 4-byte boundary.  Both are baked into the
** loops below; a port to a different padding would have to revisit them.
*/

#include <string.h>
#include "X.h"
#include "Xmd.h"
#include "Xproto.h"
#include "misc.h"
#include "fontstruct.h"
#include "dixfontstr.h"
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

extern void miPolyGlyphBlt();
extern void miImageGlyphBlt();
extern void QueryGlyphExtents();


/* Counters, reported by va2000CloseHW.  Same reason as the PutImage pair:
** a fast path that never fires and a fast path that does not help look the
** same from outside. */
long va2000_glyph_fast = 0;
long va2000_glyph_slow = 0;
long va2000_glyph_px   = 0;

/* ------------------------------------------------------------------ */

/*
** glyphFg — write one glyph's set bits, no clipping.
**
** The caller has already established that the glyph lies entirely inside
** the drawable and inside one clip box, so this is the inner loop and
** nothing else.  Rows are handled eight pixels at a time because that is
** one byte of glyph; the `if (bits)` skips the blank byte that most of a
** proportional font's rows end with.
*/
static void
glyphFg(dst, stride, gbits, gw, gh, gstride, pixel)
unsigned short *dst;
int             stride;
unsigned char  *gbits;
int             gw, gh, gstride;
unsigned short  pixel;
{
    unsigned char  *gp;
    unsigned short *dp;
    unsigned int    bits;
    int             row, col;

    va2000_glyph_px += (long) gw * (long) gh;

    for (row = 0; row < gh; row++)
    {
        gp = gbits + row * gstride;
        dp = dst + row * stride;
        col = gw;

        while (col >= 8)
        {
            bits = (unsigned int) *gp++;
            if (bits)
            {
                if (bits & 0x80) dp[0] = pixel;
                if (bits & 0x40) dp[1] = pixel;
                if (bits & 0x20) dp[2] = pixel;
                if (bits & 0x10) dp[3] = pixel;
                if (bits & 0x08) dp[4] = pixel;
                if (bits & 0x04) dp[5] = pixel;
                if (bits & 0x02) dp[6] = pixel;
                if (bits & 0x01) dp[7] = pixel;
            }
            dp  += 8;
            col -= 8;
        }

        if (col > 0)
        {
            unsigned int mask = 0x80;
            bits = (unsigned int) *gp;
            while (col-- > 0)
            {
                if (bits & mask)
                    *dp = pixel;
                dp++;
                mask >>= 1;
            }
        }
    }
}

/*
** textBox — the rectangle a run of glyphs will touch, in absolute screen
** coordinates.  Returns FALSE if the run is empty.
*/
static Bool
textBox(pGC, x, y, nglyph, ppci, box)
GCPtr        pGC;
int          x, y;
unsigned int nglyph;
CharInfoPtr *ppci;
BoxPtr       box;
{
    ExtentInfoRec info;

    if (nglyph == 0)
        return FALSE;

    QueryGlyphExtents(pGC->font, ppci, (unsigned long) nglyph, &info);

    box->x1 = x + info.overallLeft;
    box->x2 = x + info.overallRight;
    box->y1 = y - info.overallAscent;
    box->y2 = y + info.overallDescent;

    return (box->x1 < box->x2 && box->y1 < box->y2);
}

/*
** oneBox — TRUE when the whole rectangle sits inside a single clip box.
**
** Anything else -- split across two boxes, or partly outside -- goes to mi.
** Clipping per glyph here would be a second inner loop to get right for a
** case that happens when a window edge crosses a line of text.
*/
static Bool
oneBox(clip, box)
RegionPtr clip;
BoxPtr    box;
{
    BoxPtr p;
    int    n;

    n = REGION_NUM_RECTS(clip);
    p = REGION_RECTS(clip);
    while (n--)
    {
        if (box->x1 >= p->x1 && box->x2 <= p->x2 &&
            box->y1 >= p->y1 && box->y2 <= p->y2)
            return TRUE;
        p++;
    }
    return FALSE;
}

/*
** usable — the conditions the fast path needs from the GC and drawable.
*/
static Bool
usable(pDraw, pGC)
DrawablePtr pDraw;
GCPtr       pGC;
{
    return (VA2000_OPT(VA2000_OPT_GLYPH)      &&
            pDraw->depth  == VA2000_DEPTH     &&
            pGC->depth    == VA2000_DEPTH     &&
            pGC->alu      == GXcopy           &&
            pGC->fillStyle == FillSolid       &&
            (pGC->planemask & 0xffffUL) == 0xffffUL);
}

/*
** drawGlyphs — the common tail of both entry points.
*/
static void
drawGlyphs(pDraw, pGC, x, y, nglyph, ppci, pglyphBase, pixel)
DrawablePtr    pDraw;
GCPtr          pGC;
int            x, y;
unsigned int   nglyph;
CharInfoPtr   *ppci;
pointer        pglyphBase;
unsigned short pixel;
{
    rtgScreenPtr    pRTG = GetRTGScreen(pDraw->pScreen);
    unsigned short *base;
    int             stride;
    CharInfoPtr     pci;
    unsigned char  *gbits;
    int             gw, gh, gstride;

    base   = (pDraw->type == DRAWABLE_WINDOW)
             ? pRTG->fbBase
             : (unsigned short *) ((PixmapPtr) pDraw)->devPrivate.ptr;
    stride = (pDraw->type == DRAWABLE_WINDOW)
             ? va2000_selected_mode->w
             : (int) ((PixmapPtr) pDraw)->devKind / 2;

    while (nglyph--)
    {
        pci = *ppci++;
        gw  = GLYPHWIDTHPIXELS(pci);
        gh  = GLYPHHEIGHTPIXELS(pci);

        if (gw && gh)
        {
            gbits   = FONTGLYPHBITS(pglyphBase, pci);
            gstride = GLYPHWIDTHBYTESPADDED(pci);

            glyphFg(base
                      + (y - pci->metrics.ascent) * stride
                      + (x + pci->metrics.leftSideBearing),
                    stride, gbits, gw, gh, gstride, pixel);
        }
        x += pci->metrics.characterWidth;
    }
}

/* ------------------------------------------------------------------ */

/*
** va2000PolyGlyphBlt — foreground only, background untouched.
*/
void
va2000PolyGlyphBlt(pDraw, pGC, x, y, nglyph, ppci, pglyphBase)
DrawablePtr  pDraw;
GCPtr        pGC;
int          x, y;
unsigned int nglyph;
CharInfoPtr *ppci;
pointer      pglyphBase;
{
    BoxRec box;
    int    ax, ay;

    ax = x;
    ay = y;
    if (pGC->miTranslate)
    {
        ax += pDraw->x;
        ay += pDraw->y;
    }

    if (!usable(pDraw, pGC) ||
        !textBox(pGC, ax, ay, nglyph, ppci, &box) ||
        !oneBox(rtgGCClip(pGC), &box))
    {
        va2000_glyph_slow++;
        miPolyGlyphBlt(pDraw, pGC, x, y, nglyph, ppci, pglyphBase);
        return;
    }

    va2000_glyph_fast++;
    drawGlyphs(pDraw, pGC, ax, ay, nglyph, ppci, pglyphBase,
               (unsigned short) pGC->fgPixel);
}

/*
** va2000ImageGlyphBlt — background rectangle, then foreground.
**
** Two passes rather than one.  A single pass writing either colour per
** pixel would save a little, but the background rectangle is wider than
** the glyph boxes -- it is the font's full ascent-to-descent band across
** the whole run -- so the parts of it no glyph covers would need a second
** loop anyway.  Filling first is simpler and reuses the span writer, which
** already writes two pixels per store.
*/
void
va2000ImageGlyphBlt(pDraw, pGC, x, y, nglyph, ppci, pglyphBase)
DrawablePtr  pDraw;
GCPtr        pGC;
int          x, y;
unsigned int nglyph;
CharInfoPtr *ppci;
pointer      pglyphBase;
{
    ExtentInfoRec info;
    BoxRec        box, back;
    int           ax, ay, row;

    ax = x;
    ay = y;
    if (pGC->miTranslate)
    {
        ax += pDraw->x;
        ay += pDraw->y;
    }

    if (!usable(pDraw, pGC) ||
        !textBox(pGC, ax, ay, nglyph, ppci, &box) ||
        !oneBox(rtgGCClip(pGC), &box))
    {
        va2000_glyph_slow++;
        miImageGlyphBlt(pDraw, pGC, x, y, nglyph, ppci, pglyphBase);
        return;
    }

    /*
    ** The background band, as ImageText defines it: the run's advance
    ** width, and the font's ascent and descent rather than the glyphs'.
    */
    QueryGlyphExtents(pGC->font, ppci, (unsigned long) nglyph, &info);

    if (info.overallWidth >= 0)
    {
        back.x1 = ax;
        back.x2 = ax + info.overallWidth;
    }
    else
    {
        back.x1 = ax + info.overallWidth;
        back.x2 = ax;
    }
    back.y1 = ay - FONTASCENT(pGC->font);
    back.y2 = ay + FONTDESCENT(pGC->font);

    /*
    ** The band can be taller than the glyph box, so it needs its own clip
    ** check; if it does not fit in one box either, let mi have the call.
    */
    if (back.x1 >= back.x2 || back.y1 >= back.y2 ||
        !oneBox(rtgGCClip(pGC), &back))
    {
        va2000_glyph_slow++;
        miImageGlyphBlt(pDraw, pGC, x, y, nglyph, ppci, pglyphBase);
        return;
    }

    va2000_glyph_fast++;

    /* Background band.  It is known to fit in one clip box, so the rows go
    ** straight down through the shared run filler -- which writes two
    ** pixels per store -- with no re-clipping and no GC to disturb. */
    {
        rtgScreenPtr    pRTG = GetRTGScreen(pDraw->pScreen);
        unsigned short *base;
        int             stride;
        unsigned short  bg = (unsigned short) pGC->bgPixel;

        base   = (pDraw->type == DRAWABLE_WINDOW)
                 ? pRTG->fbBase
                 : (unsigned short *) ((PixmapPtr) pDraw)->devPrivate.ptr;
        stride = (pDraw->type == DRAWABLE_WINDOW)
                 ? va2000_selected_mode->w
                 : (int) ((PixmapPtr) pDraw)->devKind / 2;

        for (row = back.y1; row < back.y2; row++)
            va2000FillRun(base + row * stride + back.x1, bg,
                          back.x2 - back.x1);
    }

    drawGlyphs(pDraw, pGC, ax, ay, nglyph, ppci, pglyphBase,
               (unsigned short) pGC->fgPixel);
}
