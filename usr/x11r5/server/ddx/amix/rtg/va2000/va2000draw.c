/*
** va2000draw.c — VA2000 drawing primitives for xamix-rtg.
**
** Direct 16-bit pixel writes to the mmap'd framebuffer at
** pRTG->fbBase (pixel 0,0), pitch = VA2000_PITCH bytes per scanline.
**
** All span coordinates are absolute screen coordinates (miTranslate=TRUE).
** Rectangle coordinates from PolyFillRect are drawable-relative and
** are adjusted by pDrawable->x / pDrawable->y before use.
** Clipping uses rtgGCClip(pGC) which ValidateGC keeps up to date.
**
** Functions that operate without a GC (PaintWindow, CopyWindow) clip
** against the region passed by the caller.
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

/* ---------------------------------------------------------------------- */
/* Private helpers                                                         */

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#define VA2000_STRIDE  (va2000_selected_mode->w)  /* pixels per scanline */

/* ---------------------------------------------------------------------- */
/* Hardware blitter helpers                                                 */

/*
** Number of times va2000BlitWait gave up.  Reported by va2000CloseHW.
** Non-zero here means the blitter status register did not clear, which is
** the failure mode behind the SolidRect hang recorded in README.
*/
long va2000_blit_timeouts = 0;
long va2000_blit_slow     = 0;
long va2000_blit_worst    = 0;

/* Work counters -- see the comment on these in va2000.h. */
long va2000_blits       = 0;
long va2000_blit_polls  = 0;
long va2000_fill_hw_px  = 0;
long va2000_fill_cpu_px = 0;
long va2000_copy_hw_px  = 0;
long va2000_copy_cpu_px = 0;
long va2000_image_px    = 0;
long va2000_copy_hist[VA2000_SZBUCKETS];

/*
** va2000BlitWait — wait for the blitter, but not forever.
**
** The original loop had no bound, so a blitter that never cleared its
** enable register locked the whole server and took the AMIX screen manager
** down with it.  Giving up instead can leave a visual artefact — the next
** blit's register writes may land while the previous one is still running —
** but a repaint fixes that, and a wedged server needs a cold boot.
**
** The bound is far above any legitimate blit: filling 1920x1080 is about
** two million pixels, and one poll costs at least a Zorro register read,
** so real work cannot reach it.
*/
static void
va2000BlitWait(regBase)
pointer regBase;
{
    long n = 0;

    while (VA2000_READREG(regBase, VA2000_BLT_ENABLE) != 0)
    {
        if (++n >= VA2000_BLIT_TIMEOUT)
        {
            va2000_blit_timeouts++;
            if (va2000_blit_timeouts <= VA2000_BLIT_TIMEOUT_LOG)
                ErrorF("va2000: blitter timeout, enable=0x%x (giving up)\n",
                       (int) VA2000_READREG(regBase, VA2000_BLT_ENABLE));
            break;
        }
    }

    va2000_blit_polls += n;

    if (n > va2000_blit_worst)
        va2000_blit_worst = n;

    if (n >= VA2000_BLIT_SLOW)
    {
        va2000_blit_slow++;
        if (va2000_blit_slow <= VA2000_BLIT_TIMEOUT_LOG)
            ErrorF("va2000: slow blit, %d polls (~%d ms)\n",
                   (int) n, (int)(n / 5000));
    }
}

#define BLITWAIT(base)  va2000BlitWait((pointer)(base))

/*
** BLITSRC — SDRAM word address of row y, pixel 0.
** FPGA SDRAM word 0 == framebuffer pixel (0,0); Zorro addresses are mapped
** as (z3addr - z3_ram_low) >> 1, so VA2000_FB_WORDS is NOT added.
** Formula verified against blit_test4.c (the authoritative reference).
*/
#define BLITSRC_HI(y) \
    ((unsigned short)(((unsigned long)(y) * va2000_selected_mode->w) >> 16))
#define BLITSRC_LO(y) \
    ((unsigned short)(((unsigned long)(y) * va2000_selected_mode->w) & 0xffff))

/*
** blitFill — fill a screen rectangle with a solid colour via hardware.
** All coordinates are absolute screen pixels; (x2,y2) are inclusive.
** Sequence from va2000_blittest.c (confirmed working on this hardware).
*/
static void
blitFill(regBase, x1, y1, x2, y2, color)
pointer        regBase;
unsigned short x1, y1, x2, y2, color;
{
    va2000_blits++;
    va2000_fill_hw_px += (long)(x2 - x1 + 1) * (long)(y2 - y1 + 1);
    BLITWAIT(regBase);
    VA2000_WRITEREG(regBase, VA2000_BLT_SRC_HI,   BLITSRC_HI(y1));
    VA2000_WRITEREG(regBase, VA2000_BLT_SRC_LO,   BLITSRC_LO(y1));
    VA2000_WRITEREG(regBase, VA2000_BLT_ROWPITCH,  va2000_selected_mode->w);
    VA2000_WRITEREG(regBase, VA2000_BLT_COLORMODE, 1);
    VA2000_WRITEREG(regBase, VA2000_BLT_RGB16,     color);
    VA2000_WRITEREG(regBase, VA2000_BLT_X1,        x1);
    VA2000_WRITEREG(regBase, VA2000_BLT_Y1,        y1);
    VA2000_WRITEREG(regBase, VA2000_BLT_X2,        x2);
    VA2000_WRITEREG(regBase, VA2000_BLT_Y2,        y2);
    VA2000_WRITEREG(regBase, VA2000_BLT_ENABLE,    VA2000_BLT_FILL);
    BLITWAIT(regBase);
}

/*
** blitCopy — screen-to-screen blit via hardware.
**
** Register layout from FPGA Verilog (va2000-spartan6/va2000.v):
**   0x40/0x42 = blitter_base  — SDRAM word addr of destination STARTING row
**   0x44/0x46 = blitter_base2 — SDRAM word addr of source STARTING row
**   0x1c      = blitter_row_pitch
**
** Caller controls copy direction by the order of X/Y coordinates:
**   Forward (Y1<Y2, Y3<Y4): top-to-bottom — for non-overlapping or upward moves
**   Reverse (Y1>Y2, Y3>Y4): bottom-to-top — for downward overlapping moves
**   dbase / sbase must point to the STARTING row (not necessarily the topmost).
*/
static void
blitCopy(regBase, x1, y1, x2, y2, x3, y3, x4, y4, dbase, sbase)
pointer        regBase;
unsigned short x1, y1, x2, y2;  /* destination start → end (inclusive) */
unsigned short x3, y3, x4, y4;  /* source start → end (inclusive) */
unsigned long  dbase, sbase;     /* SDRAM word addr of dest/src starting row */
{
    va2000_blits++;
    va2000_copy_hw_px += (long)(x2 - x1 + 1) * (long)(y2 - y1 + 1);
    BLITWAIT(regBase);
    VA2000_WRITEREG(regBase, VA2000_BLT_SRC_HI,   (unsigned short)(dbase >> 16));
    VA2000_WRITEREG(regBase, VA2000_BLT_SRC_LO,   (unsigned short)(dbase & 0xffff));
    VA2000_WRITEREG(regBase, VA2000_BLT_SRC2_HI,  (unsigned short)(sbase >> 16));
    VA2000_WRITEREG(regBase, VA2000_BLT_SRC2_LO,  (unsigned short)(sbase & 0xffff));
    VA2000_WRITEREG(regBase, VA2000_BLT_ROWPITCH,  va2000_selected_mode->w);
    VA2000_WRITEREG(regBase, VA2000_BLT_COLORMODE, 1);
    VA2000_WRITEREG(regBase, VA2000_BLT_X1,        x1);
    VA2000_WRITEREG(regBase, VA2000_BLT_Y1,        y1);
    VA2000_WRITEREG(regBase, VA2000_BLT_X2,        x2);
    VA2000_WRITEREG(regBase, VA2000_BLT_Y2,        y2);
    VA2000_WRITEREG(regBase, VA2000_BLT_X3,        x3);
    VA2000_WRITEREG(regBase, VA2000_BLT_Y3,        y3);
    VA2000_WRITEREG(regBase, VA2000_BLT_X4,        x4);
    VA2000_WRITEREG(regBase, VA2000_BLT_Y4,        y4);
    VA2000_WRITEREG(regBase, VA2000_BLT_ENABLE,    VA2000_BLT_COPY);
    BLITWAIT(regBase);
}

/*
** Pixel base and stride for a drawable (window → VRAM; pixmap → host mem).
*/
static unsigned short *
drawBase(pDraw, pRTG)
DrawablePtr  pDraw;
rtgScreenPtr pRTG;
{
    if (pDraw->type == DRAWABLE_WINDOW)
        return pRTG->fbBase;
    return (unsigned short *) ((PixmapPtr)pDraw)->devPrivate.ptr;
}

static int
drawStride(pDraw)
DrawablePtr pDraw;
{
    if (pDraw->type == DRAWABLE_WINDOW)
        return VA2000_STRIDE;
    return (int)((PixmapPtr)pDraw)->devKind / 2;
}

/*
** copyVRAMBox — copy a screen rectangle via hardware blitter.
** blitter_base / blitter_base2 (0x40-0x46) supply the starting-row SDRAM
** word addresses; X/Y coordinate order controls copy direction so that
** overlapping moves are handled correctly by the hardware.
*/
static void
copyVRAMBox(regBase, fbBase, stride, srcx, srcy, w, h, dstx, dsty)
pointer         regBase;
unsigned short *fbBase;
int             stride;
int             srcx, srcy, w, h, dstx, dsty;
{
    unsigned long dbase, sbase;

    if (w <= 0 || h <= 0)
        return;

    if (dsty < srcy || (dsty == srcy && dstx <= srcx))
    {
        /* Forward: top row first, left column first */
        dbase = (unsigned long) dsty * va2000_selected_mode->w;
        sbase = (unsigned long) srcy * va2000_selected_mode->w;
        blitCopy(regBase,
            (unsigned short) dstx,       (unsigned short) dsty,
            (unsigned short)(dstx+w-1),  (unsigned short)(dsty+h-1),
            (unsigned short) srcx,       (unsigned short) srcy,
            (unsigned short)(srcx+w-1),  (unsigned short)(srcy+h-1),
            dbase, sbase);
    }
    else
    {
        /* Reverse: bottom row first, right column first */
        dbase = (unsigned long)(dsty + h - 1) * va2000_selected_mode->w;
        sbase = (unsigned long)(srcy + h - 1) * va2000_selected_mode->w;
        blitCopy(regBase,
            (unsigned short)(dstx+w-1),  (unsigned short)(dsty+h-1),
            (unsigned short) dstx,       (unsigned short) dsty,
            (unsigned short)(srcx+w-1),  (unsigned short)(srcy+h-1),
            (unsigned short) srcx,       (unsigned short) srcy,
            dbase, sbase);
    }
}

/*
** Fill a horizontal run of w pixels with a solid 16-bit colour.
** Not static: va2000text.c paints the ImageText background band with it.
*/
void
va2000FillRun(dst, pixel, w)
unsigned short *dst;
unsigned short  pixel;
int             w;
{
    unsigned long *lp;
    unsigned long  two;

    /*
    ** Two pixels per store.  The card is 16-bit but the bus is not: in
    ** Zorro III a longword write moves both pixels in one transaction, and
    ** even in Zorro II, where the bus splits it into two word cycles, the
    ** CPU-side cost halves.  va2000_bench measured 4.87 MB/s for 32-bit
    ** stores against 2.55 MB/s for 16-bit ones on this board.
    **
    ** The alignment step is not optional: a longword store to an address
    ** that is only 2-byte aligned is slower than the two word stores it
    ** replaces, so a run that starts on an odd pixel writes that pixel
    ** singly first.  Short runs skip the whole thing -- the setup costs
    ** more than it saves below about eight pixels.
    */
    va2000_fill_cpu_px += w;

    if (w < 8 || !VA2000_OPT(VA2000_OPT_LONGWORD))
    {
        while (w-- > 0)
            *dst++ = pixel;
        return;
    }

    if (((unsigned long)(char *) dst) & 2UL)
    {
        *dst++ = pixel;
        w--;
    }

    two = ((unsigned long) pixel << 16) | (unsigned long) pixel;
    lp  = (unsigned long *) dst;

    while (w >= 8)
    {
        lp[0] = two; lp[1] = two; lp[2] = two; lp[3] = two;
        lp += 4;
        w  -= 8;
    }
    while (w >= 2)
    {
        *lp++ = two;
        w -= 2;
    }

    dst = (unsigned short *) lp;
    while (w-- > 0)
        *dst++ = pixel;
}

/*
** va2000XorRun -- XOR a pixel across a run, two pixels per bus cycle.
**
** GXxor and GXinvert are the two raster ops that a whole window's worth of
** pixels can land on, and they are the expensive kind: a read-modify-write
** touches VRAM twice per pixel, and a read across Zorro cannot be posted the
** way a write can, so it is not the same cost as a fill.
**
** xterm's visual bell is the case that shows it.  It XORs the entire window,
** flushes, and XORs it back -- at 80x24 in 8x13 that is 200 000 pixels twice
** over.  Word at a time that is 800 000 bus round trips for one flash.
** Longword at a time it is half that, on the same terms as va2000FillRun:
** align first, and do not bother for short runs.
*/
void
va2000XorRun(dst, pixel, w)
unsigned short *dst;
unsigned short  pixel;
int             w;
{
    unsigned long *lp;
    unsigned long  two;

    if (w < 8 || !VA2000_OPT(VA2000_OPT_LONGWORD))
    {
        while (w-- > 0)
        {
            *dst ^= pixel;
            dst++;
        }
        return;
    }

    if (((unsigned long)(char *) dst) & 2UL)
    {
        *dst ^= pixel;
        dst++;
        w--;
    }

    two = ((unsigned long) pixel << 16) | (unsigned long) pixel;
    lp  = (unsigned long *) dst;

    while (w >= 8)
    {
        lp[0] ^= two; lp[1] ^= two; lp[2] ^= two; lp[3] ^= two;
        lp += 4;
        w  -= 8;
    }
    while (w >= 2)
    {
        *lp ^= two;
        lp++;
        w -= 2;
    }

    dst = (unsigned short *) lp;
    while (w-- > 0)
    {
        *dst ^= pixel;
        dst++;
    }
}

/*
** va2000CopyRun — copy n pixels, two per VRAM store.
**
** memcpy was here, and the saku26 project's advice to look at what this
** machine's memcpy does turned out to be the whole story: PutImage measured
** 3.07 us per pixel through memcpy while va2000_bench writes the same board
** at 0.79 us per pixel with 16-bit stores and 0.41 with 32-bit ones.
**
** The first version of this function required both ends to share a 2-byte
** phase before it would use longwords, and about half of PutImage's calls
** did not: the destination phase follows the x coordinate, the source phase
** does not, so the alignment agreed or disagreed depending on where the
** image landed.  Those calls fell back to word stores and the measurement
** showed it -- 2.04 us per pixel where the board can do 0.41.
**
** So align the DESTINATION and let the source be whatever it is.  When the
** source is not longword-aligned the pair is assembled from two host reads
** and written as one longword.  Two reads from RAM the CPU has cached
** against one transaction on the Zorro bus is a trade worth making every
** time.
*/
void
va2000CopyRun(dst, src, n)
unsigned short *dst;
unsigned short *src;
int             n;
{
    unsigned long *ld;

    if (n <= 0)
        return;

    va2000_copy_cpu_px += n;

    if (n < 8 || !VA2000_OPT(VA2000_OPT_LONGWORD))
    {
        while (n-- > 0)
            *dst++ = *src++;
        return;
    }

    /* An unaligned longword store costs more than the two word stores it
    ** replaces, so pay one word to get onto a 4-byte boundary. */
    if (((unsigned long)(char *) dst) & 2UL)
    {
        *dst++ = *src++;
        n--;
    }

    ld = (unsigned long *) dst;

    if ((((unsigned long)(char *) src) & 2UL) == 0)
    {
        unsigned long *ls = (unsigned long *) src;

        while (n >= 8)
        {
            ld[0] = ls[0]; ld[1] = ls[1]; ld[2] = ls[2]; ld[3] = ls[3];
            ld += 4;
            ls += 4;
            n  -= 8;
        }
        while (n >= 2)
        {
            *ld++ = *ls++;
            n -= 2;
        }
        src = (unsigned short *) ls;
    }
    else
    {
        /*
        ** MSBFirst: in a longword written to memory the high half lands at
        ** the lower address, so the first pixel is the one shifted up.
        */
        while (n >= 8)
        {
            ld[0] = ((unsigned long) src[0] << 16) | (unsigned long) src[1];
            ld[1] = ((unsigned long) src[2] << 16) | (unsigned long) src[3];
            ld[2] = ((unsigned long) src[4] << 16) | (unsigned long) src[5];
            ld[3] = ((unsigned long) src[6] << 16) | (unsigned long) src[7];
            ld  += 4;
            src += 8;
            n   -= 8;
        }
        while (n >= 2)
        {
            *ld++ = ((unsigned long) src[0] << 16) | (unsigned long) src[1];
            src += 2;
            n   -= 2;
        }
    }

    dst = (unsigned short *) ld;
    while (n-- > 0)
        *dst++ = *src++;
}

/* ---------------------------------------------------------------------- */
/* va2000GetImage — tracing wrapper for miGetImage                        */

extern void miGetImage();
extern void mfbGetSpans();

void
va2000GetImage(pDraw, sx, sy, w, h, format, planeMask, pdstLine)
DrawablePtr    pDraw;
int            sx, sy, w, h;
unsigned int   format;
unsigned long  planeMask;
pointer        pdstLine;
{
    miGetImage(pDraw, sx, sy, w, h, format, planeMask, pdstLine);
}

/* ---------------------------------------------------------------------- */
/* va2000PutImage                                                          */

extern void miPutImage();
extern RegionPtr miHandleExposures();

/*
** How often the native path applied, and how often it did not.  Reported by
** va2000CloseHW.  A fast path that never fires looks exactly like a fast
** path that does not help, and the two need different fixes.
*/
long va2000_putimage_fast = 0;
long va2000_putimage_slow = 0;

/*
** va2000PutImage — native path for the case clients actually send.
**
** miPutImage takes ZPixmap data apart into spans and pushes them through
** SetSpans, one function-pointer call per scanline plus the span machinery
** around it.  Measured from the client side, that path costs three to four
** times what writing the same pixels costs: the saku26 demo timed a
** 640x360 frame at 252 ms of which 170-200 ms was neither the socket nor
** the VRAM write, and xbench sees 0.2-0.34 Mpixel/s against the 1.27
** Mpixel/s va2000_bench gets writing straight to the board.
**
** So: when the format, depth and function are the ones a client pushing
** pixels actually uses, copy the rows.  Everything else -- XYPixmap,
** XYBitmap, a depth that is not the screen's, a non-copy alu, a planemask
** with holes in it, a left pad -- falls through to mi, which is correct
** for all of them and rare in practice.
**
** X pads each source scanline to a 4-byte boundary; PixmapBytePad is the
** same expression SetSpans steps by, and getting it wrong is what made
** odd-width images come out skewed.
*/
void
va2000PutImage(pDraw, pGC, depth, x, y, w, h, leftPad, format, pImage)
DrawablePtr pDraw;
GCPtr       pGC;
int         depth;
int         x, y, w, h;
int         leftPad;
int         format;
char       *pImage;
{
    rtgScreenPtr    pRTG;
    unsigned short *base;
    int             stride;
    RegionPtr       clip;
    BoxPtr          pbox;
    int             nbox;
    int             srcBytes;
    int             absx, absy;
    int             cx1, cy1, cx2, cy2;
    int             yy, runBytes;
    char           *src;
    char           *dst;
    long            imageMark;

    if (!VA2000_OPT(VA2000_OPT_PUTIMAGE) ||
        format   != ZPixmap        ||
        depth    != VA2000_DEPTH   ||
        pDraw->depth != VA2000_DEPTH ||
        leftPad  != 0              ||
        pGC->alu != GXcopy         ||
        (pGC->planemask & 0xffffUL) != 0xffffUL ||
        w <= 0 || h <= 0)
    {
        va2000_putimage_slow++;
        miPutImage(pDraw, pGC, depth, x, y, w, h, leftPad, format, pImage);
        return;
    }

    /*
    ** Count what PutImage writes as image work, not as copy work.  It goes
    ** through the same run copier, so without this its pixels land in both
    ** totals and a session reports moving nearly twice what it moved.  Take
    ** the difference across the loop rather than w*h: the clip decides how
    ** much is actually written.
    */
    imageMark = va2000_copy_cpu_px;
    va2000_putimage_fast++;

    pRTG   = GetRTGScreen(pDraw->pScreen);
    base   = drawBase(pDraw, pRTG);
    stride = drawStride(pDraw);
    clip   = rtgGCClip(pGC);

    absx = pDraw->x + x;
    absy = pDraw->y + y;

    srcBytes = PixmapBytePad(w, depth);

    nbox = REGION_NUM_RECTS(clip);
    pbox = REGION_RECTS(clip);

    while (nbox--)
    {
        cx1 = max(absx,     pbox->x1);
        cy1 = max(absy,     pbox->y1);
        cx2 = min(absx + w, pbox->x2);
        cy2 = min(absy + h, pbox->y2);
        pbox++;

        if (cx1 >= cx2 || cy1 >= cy2)
            continue;

        runBytes = (cx2 - cx1) * 2;
        src = pImage + (cy1 - absy) * srcBytes + (cx1 - absx) * 2;
        dst = (char *)(base + cy1 * stride + cx1);

        for (yy = cy1; yy < cy2; yy++)
        {
            va2000CopyRun((unsigned short *) dst, (unsigned short *) src,
                          cx2 - cx1);
            src += srcBytes;
            dst += stride * 2;
        }
    }

    va2000_image_px    += va2000_copy_cpu_px - imageMark;
    va2000_copy_cpu_px  = imageMark;
}

/* ---------------------------------------------------------------------- */
/* va2000FillSpans                                                         */

/*
** va2000FillSpans — solid colour span fill.
**
** Called by DIX for FillSolid fill style.  Spans in ppts are in absolute
** screen coordinates (DIX translates when miTranslate=TRUE).
** miClipSpans clips the array in-place against the GC composite clip.
*/
void
va2000FillSpans(pDraw, pGC, nspans, ppts, pwidths, fSorted)
DrawablePtr pDraw;
GCPtr       pGC;
int         nspans;
DDXPointPtr ppts;
int        *pwidths;
int         fSorted;
{
    rtgScreenPtr    pRTG   = GetRTGScreen(pDraw->pScreen);
    unsigned short  pixel  = (unsigned short) pGC->fgPixel;
    int             alu    = pGC->alu;
    unsigned short *base   = drawBase(pDraw, pRTG);
    int             stride = drawStride(pDraw);
    unsigned short *p;
    int             i, w;

    nspans = miClipSpans(rtgGCClip(pGC),
                         ppts, pwidths, nspans,
                         ppts, pwidths, fSorted);

    for (i = 0; i < nspans; i++)
    {
        w = pwidths[i];
        if (w <= 0)
            continue;
        p = base + ppts[i].y * stride + ppts[i].x;

        switch (alu)
        {
        case GXcopy:         va2000FillRun(p, pixel, w);                     break;
        case GXxor:          va2000XorRun(p, pixel, w);                  break;
        case GXor:           while (w-->0) { *p |= pixel;    p++; }    break;
        case GXand:          while (w-->0) { *p &= pixel;    p++; }    break;
        case GXinvert:       va2000XorRun(p, (unsigned short) 0xFFFF, w); break;
        case GXclear:        while (w-->0)  *p++ = 0;                  break;
        case GXset:          while (w-->0)  *p++ = 0xFFFF;             break;
        case GXnoop:                                                    break;
        case GXcopyInverted: while (w-->0)  *p++ = ~pixel;             break;
        case GXandReverse:   while (w-->0) { *p = pixel & ~*p;  p++; } break;
        case GXandInverted:  while (w-->0) { *p &= ~pixel;      p++; } break;
        case GXorReverse:    while (w-->0) { *p = pixel | ~*p;  p++; } break;
        case GXnor:          while (w-->0) { *p = ~(pixel | *p); p++; } break;
        case GXequiv:        while (w-->0) { *p = ~(pixel ^ *p); p++; } break;
        case GXorInverted:   while (w-->0) { *p |= ~pixel;      p++; } break;
        case GXnand:         while (w-->0) { *p = ~(pixel & *p); p++; } break;
        default:             while (w-->0)  *p++ = pixel;              break;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* va2000SetSpans                                                          */

/*
** copyRunRop -- copy n pixels applying a raster op.
**
** SetSpans' slow path.  GXcopy goes through va2000CopyRun; the other
** fifteen land here.  This is reachable, not theoretical: va2000PutImage
** hands a non-copy alu straight to miPutImage, and miPutImage writes
** through SetSpans.  Writing the source pixels unchanged made an XOR
** PutImage paint rather than combine.
*/
static void
copyRunRop(dst, src, n, alu)
register unsigned short *dst;
register unsigned short *src;
register int             n;
int                      alu;
{
    switch (alu)
    {
    case GXclear:        while (n-->0) { *dst++ = 0; }                    break;
    case GXand:          while (n-->0) { *dst++ &= *src++; }              break;
    case GXandReverse:   while (n-->0) { *dst = *src++ & ~*dst; dst++; }  break;
    case GXandInverted:  while (n-->0) { *dst++ &= ~*src++; }             break;
    case GXnoop:                                                          break;
    case GXxor:          while (n-->0) { *dst++ ^= *src++; }              break;
    case GXor:           while (n-->0) { *dst++ |= *src++; }              break;
    case GXnor:          while (n-->0) { *dst = ~(*src++ | *dst); dst++; }break;
    case GXequiv:        while (n-->0) { *dst = ~(*src++ ^ *dst); dst++; }break;
    case GXinvert:       while (n-->0) { *dst = ~*dst; dst++; }           break;
    case GXorReverse:    while (n-->0) { *dst = *src++ | ~*dst; dst++; }  break;
    case GXcopyInverted: while (n-->0) { *dst++ = ~*src++; }              break;
    case GXorInverted:   while (n-->0) { *dst++ |= ~*src++; }             break;
    case GXnand:         while (n-->0) { *dst = ~(*src++ & *dst); dst++; }break;
    case GXset:          while (n-->0) { *dst++ = 0xFFFF; }               break;
    default:             va2000CopyRun(dst, src, n);                      break;
    }
}

/*
** va2000SetSpans — write packed 16-bit pixel data to the framebuffer.
**
** pcharsrc is a contiguous buffer of 16-bit pixels, one span after
** another.  For each input span we clip each clip-box intersection
** and copy the appropriate slice of the source.
*/
void
va2000SetSpans(pDraw, pGC, pcharsrc, ppt, pwidths, nspans, fSorted)
DrawablePtr pDraw;
GCPtr       pGC;
char       *pcharsrc;
DDXPointPtr ppt;
int        *pwidths;
int         nspans;
int         fSorted;
{
    rtgScreenPtr    pRTG   = GetRTGScreen(pDraw->pScreen);
    unsigned short *base   = drawBase(pDraw, pRTG);
    int             stride = drawStride(pDraw);
    RegionPtr       clip   = rtgGCClip(pGC);
    int             alu    = pGC->alu;
    BoxPtr          pbox;
    int             nbox;
    unsigned short *psrc;
    int             i, cx1, cx2, sw, aw;

    psrc = (unsigned short *) pcharsrc;

    for (i = 0; i < nspans; i++, ppt++, pwidths++)
    {
        sw = *pwidths;
        aw = (sw < 0) ? -sw : sw;

        if (sw > 0)
        {
            nbox = REGION_NUM_RECTS(clip);
            pbox = REGION_RECTS(clip);

            while (nbox--)
            {
                if (pbox->y1 > ppt->y || pbox->y2 <= ppt->y)
                {
                    pbox++;
                    continue;
                }

                cx1 = max(ppt->x,      pbox->x1);
                cx2 = min(ppt->x + sw, pbox->x2);

                if (cx1 < cx2)
                {
                    if (alu == GXcopy)
                        va2000CopyRun(base + ppt->y * stride + cx1,
                                      psrc + (cx1 - ppt->x),
                                      cx2 - cx1);
                    else
                        copyRunRop(base + ppt->y * stride + cx1,
                                   psrc + (cx1 - ppt->x),
                                   cx2 - cx1, alu);
                }
                pbox++;
            }
        }

        /*
        ** Advance by the PADDED scanline length, not by the pixel count.
        **
        ** X pads every PutImage scanline to a 4-byte boundary and
        ** miPutImage hands that data to SetSpans unchanged, so at depth 16
        ** an odd width carries one pixel of padding.  Stepping by sw shifted
        ** every following row left by one pixel: the image landed skewed,
        ** and because the skewed ink falls outside the box the client
        ** repaints next, it stayed on screen.  Found by the saku26 demo,
        ** which reads its own window back with XGetImage and diffs it.
        **
        ** cfbSetSpans does the same with PixmapWidthInPadUnits(); this is
        ** the 16-bit spelling of it.  va2000GetSpans pads to match -- the
        ** two have to agree, because miCopyArea fills a buffer with one and
        ** drains it with the other.
        */
        psrc += PixmapBytePad(aw, pDraw->depth) >> 1;
    }
}

/* ---------------------------------------------------------------------- */
/* va2000GetSpans                                                          */

/*
** va2000GetSpans — read pixel data from the drawable into pdstStart.
**
** No clipping: DIX clips before calling GetSpans.
** The output buffer is treated as a flat array of unsigned short.
*/
void
va2000GetSpans(pDraw, wMax, ppt, pwidth, nspans, pdstStart)
DrawablePtr    pDraw;
int            wMax;
DDXPointPtr    ppt;
int           *pwidth;
int            nspans;
unsigned long *pdstStart;
{
    rtgScreenPtr    pRTG;
    unsigned short *base;
    int             stride;
    unsigned short *dst;
    int             i, w;

    if (pDraw->depth == 1)
    {
        mfbGetSpans(pDraw, wMax, ppt, pwidth, nspans, pdstStart);
        return;
    }

    pRTG   = GetRTGScreen(pDraw->pScreen);
    base   = drawBase(pDraw, pRTG);
    stride = drawStride(pDraw);
    dst    = (unsigned short *) pdstStart;

    for (i = 0; i < nspans; i++, ppt++, pwidth++)
    {
        w = min(*pwidth, wMax);
        if (w < 0)
            w = 0;
        if (w > 0)
            va2000CopyRun(dst, base + ppt->y * stride + ppt->x, w);

        /*
        ** Padded stride, for the same reason as va2000SetSpans above:
        ** miCopyArea allocates height * PixmapBytePad(width, depth) and
        ** expects each row to start on a padded boundary, and miGetImage
        ** hands the result straight to a client that assumes X padding.
        ** cfbGetSpans advances by PixmapWidthInPadUnits(w, PSZ).
        */
        dst += PixmapBytePad(w, pDraw->depth) >> 1;
    }
}

/* ---------------------------------------------------------------------- */
/* va2000SolidRect                                                         */

/*
** va2000SolidRect — PolyFillRect with FillSolid.
**
** prects coordinates are drawable-relative; we add pDraw->x/y to get
** absolute screen coordinates before clipping against the GC clip.
**
** Window drawables: hardware blitFill (fast path).
** Pixmap drawables: CPU fill (blitter addresses VRAM, not host memory).
**
** GXcopy only.  Every other alu goes to miPolyFillRect, which comes back
** through FillSpans -- and that one does implement the full sixteen.
**
** This routine used to write pGC->fgPixel whatever the alu said, which is
** right for GXcopy and wrong for the other fifteen.  GXxor is the one that
** shows: twm draws its move and resize outline by XORing a few thin
** rectangles onto the root and erases them by XORing the same rectangles
** again.  Painting solid both times draws the outline and then draws it a
** second time, so the frame stayed on screen after every drag -- the
** fragments around a title bar's resize corner.
*/
extern void miPolyFillRect();

void
va2000SolidRect(pDraw, pGC, nrects, prects)
DrawablePtr pDraw;
GCPtr       pGC;
int         nrects;
xRectangle *prects;
{
    rtgScreenPtr    pRTG   = GetRTGScreen(pDraw->pScreen);
    unsigned short  pixel  = (unsigned short) pGC->fgPixel;
    unsigned short *base   = drawBase(pDraw, pRTG);
    int             stride = drawStride(pDraw);
    RegionPtr       clip   = rtgGCClip(pGC);
    int             useHW  = (pDraw->type == DRAWABLE_WINDOW);
    BoxPtr          pbox;
    int             nbox;
    int             xorg   = pDraw->x;
    int             yorg   = pDraw->y;
    int             rx1, ry1, rx2, ry2;
    int             cx1, cy1, cx2, cy2;
    int             y, w, h;

    if (pGC->alu != GXcopy || (pGC->planemask & 0xffffUL) != 0xffffUL)
    {
        miPolyFillRect(pDraw, pGC, nrects, prects);
        return;
    }

    while (nrects--)
    {
        rx1 = xorg + (int) prects->x;
        ry1 = yorg + (int) prects->y;
        rx2 = rx1  + (int) prects->width;
        ry2 = ry1  + (int) prects->height;
        prects++;

        nbox = REGION_NUM_RECTS(clip);
        pbox = REGION_RECTS(clip);

        while (nbox--)
        {
            cx1 = max(rx1, pbox->x1);
            cy1 = max(ry1, pbox->y1);
            cx2 = min(rx2, pbox->x2);
            cy2 = min(ry2, pbox->y2);

            if (cx1 < cx2 && cy1 < cy2)
            {
                if (useHW && (cx2 - cx1) * (cy2 - cy1) >= va2000_blit_min)
                {
                    blitFill(pRTG->frameBase,
                             (unsigned short) cx1,
                             (unsigned short) cy1,
                             (unsigned short)(cx2 - 1),
                             (unsigned short)(cy2 - 1),
                             pixel);
                }
                else
                {
                    w = cx2 - cx1;
                    h = cy2 - cy1;
                    for (y = cy1; y < cy1 + h; y++)
                        va2000FillRun(base + y * stride + cx1, pixel, w);
                }
            }
            pbox++;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* va2000PaintWindow                                                       */

/*
** va2000PaintWindow — paint window background (PW_BACKGROUND) or border
** (PW_BORDER).
**
** Fast path: solid pixel → direct VRAM fill.
** Pixmap backgrounds are not yet tiled; the window is left as-is
** (garbage on first expose).  TODO: implement tile/stipple painting.
*/
long va2000_paint_us    = 0;
long va2000_paint_calls = 0;

static void paintWindowBody();

/*
** va2000PaintWindow -- timing wrapper around the real thing.
**
** Timed because the pixel counters cannot answer the question they were
** asked.  A long stretch awake that moved a screen's worth of tile pixels
** looks identical whether the driver was slow or whether it was barely
** involved and the time went elsewhere -- the counters count pixels, not
** time.  PaintWindow runs once per exposed region rather than once per row,
** so a clock read at each end costs nothing worth counting and settles it.
**
** A wrapper rather than three edits: the body has two early returns and
** would have grown a third place to forget.
*/
void
va2000PaintWindow(pWin, prgn, what)
WindowPtr pWin;
RegionPtr prgn;
int       what;
{
    struct timeval t0, t1;

    va2000_paint_calls++;
    gettimeofday(&t0, (struct timezone *) 0);

    paintWindowBody(pWin, prgn, what);

    gettimeofday(&t1, (struct timezone *) 0);
    va2000_paint_us += (t1.tv_sec - t0.tv_sec) * 1000000L
                     + (t1.tv_usec - t0.tv_usec);
}

static void
paintWindowBody(pWin, prgn, what)
WindowPtr pWin;
RegionPtr prgn;
int       what;
{
    rtgScreenPtr    pRTG   = GetRTGScreen(pWin->drawable.pScreen);
    unsigned short  pixel;
    BoxPtr          pbox;
    int             nbox;

    /*
    ** A pixmap background or border is tiled, not approximated.
    **
    ** This used to read the tile's first pixel and fill the region with it,
    ** which meant a root window backdrop -- what `xv -root` and
    ** `xsetroot -bitmap` set -- turned into one flat colour the first time
    ** anything exposed it.  The pattern is anchored at the window origin,
    ** which is what X specifies for both background and border.
    */
    if (VA2000_OPT(VA2000_OPT_TILE))
    {
        PixmapPtr pTile = (PixmapPtr) 0;

        if (what == PW_BACKGROUND)
        {
            if (pWin->backgroundState == BackgroundPixmap)
                pTile = pWin->background.pixmap;
        }
        else if (!pWin->borderIsPixel)
            pTile = pWin->border.pixmap;

        if (pTile && pTile->drawable.depth == VA2000_DEPTH)
        {
            va2000TileBoxes(&pWin->drawable, prgn, pTile,
                            pWin->drawable.x, pWin->drawable.y);
            return;
        }
    }

    if (what == PW_BACKGROUND)
    {
        switch (pWin->backgroundState)
        {
        case None:
            return;
        case BackgroundPixel:
            pixel = (unsigned short) pWin->background.pixel;
            break;
        case ParentRelative:
            /* Walk up to find the nearest solid background */
            {
                WindowPtr p = pWin->parent;
                while (p && p->backgroundState == ParentRelative)
                    p = p->parent;
                if (p && p->backgroundState == BackgroundPixel)
                    pixel = (unsigned short) p->background.pixel;
                else
                    pixel = (unsigned short) pWin->drawable.pScreen->blackPixel;
            }
            break;
        default:
            /* BackgroundPixmap: read first pixel from tile for solid approx. */
            {
                PixmapPtr   pTile  = pWin->background.pixmap;
                unsigned short *tp = (unsigned short *) pTile->devPrivate.ptr;
                pixel = (pTile && pTile->drawable.depth == VA2000_DEPTH && tp)
                        ? *tp
                        : (unsigned short) pWin->drawable.pScreen->blackPixel;
            }
            break;
        }
    }
    else /* PW_BORDER */
    {
        if (pWin->borderIsPixel)
            pixel = (unsigned short) pWin->border.pixel;
        else
        {
            /* Pixmap border: read first pixel for solid approx. */
            PixmapPtr   pTile  = pWin->border.pixmap;
            unsigned short *tp = (unsigned short *) pTile->devPrivate.ptr;
            pixel = (pTile && pTile->drawable.depth == VA2000_DEPTH && tp)
                    ? *tp
                    : (unsigned short) pWin->drawable.pScreen->blackPixel;
        }
    }

    nbox = REGION_NUM_RECTS(prgn);
    pbox = REGION_RECTS(prgn);

    {
        pointer regBase = pRTG->frameBase;
        while (nbox--)
        {
            int bw = pbox->x2 - pbox->x1;
            int bh = pbox->y2 - pbox->y1;

            if (bw > 0 && bh > 0)
            {
                if (bw * bh >= va2000_blit_min)
                    blitFill(regBase,
                             (unsigned short) pbox->x1,
                             (unsigned short) pbox->y1,
                             (unsigned short)(pbox->x2 - 1),
                             (unsigned short)(pbox->y2 - 1),
                             pixel);
                else
                {
                    int y;
                    for (y = pbox->y1; y < pbox->y2; y++)
                        va2000FillRun(pRTG->fbBase + y * VA2000_STRIDE
                                        + pbox->x1, pixel, bw);
                }
            }
            pbox++;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* va2000CopyWindow                                                        */

/*
** va2000CopyWindow — move window contents after the window is repositioned.
**
** Modelled on cfbCopyWindow:
**   1. Compute the destination region by intersecting prgnSrc (translated
**      to the new position) with pWin->borderClip.
**   2. Build a pptSrc[] array: source origin for each destination box.
**   3. Copy boxes in direction-aware order (bottom-to-top for downward
**      moves, reverse for rightward moves) to handle VRAM overlap.
*/
void
va2000CopyWindow(pWin, ptOldOrg, prgnSrc)
WindowPtr   pWin;
DDXPointRec ptOldOrg;
RegionPtr   prgnSrc;
{
    rtgScreenPtr  pRTG = GetRTGScreen(pWin->drawable.pScreen);
    ScreenPtr     pScreen = pWin->drawable.pScreen;
    RegionPtr     prgnDst;
    DDXPointPtr   pptSrc, ppt;
    BoxPtr        pbox;
    int           nbox, i;
    int           dx, dy;

    dx = ptOldOrg.x - pWin->drawable.x;
    dy = ptOldOrg.y - pWin->drawable.y;

    prgnDst = (*pScreen->RegionCreate)(NULL, 1);

    (*pScreen->TranslateRegion)(prgnSrc, -dx, -dy);
    (*pScreen->Intersect)(prgnDst, &pWin->borderClip, prgnSrc);

    nbox = REGION_NUM_RECTS(prgnDst);
    if (nbox == 0)
    {
        (*pScreen->RegionDestroy)(prgnDst);
        return;
    }

    pptSrc = (DDXPointPtr) ALLOCATE_LOCAL(nbox * sizeof(DDXPointRec));
    if (!pptSrc)
    {
        (*pScreen->RegionDestroy)(prgnDst);
        return;
    }

    pbox = REGION_RECTS(prgnDst);
    ppt  = pptSrc;
    for (i = 0; i < nbox; i++, ppt++, pbox++)
    {
        ppt->x = pbox->x1 + dx;
        ppt->y = pbox->y1 + dy;
    }

    pbox = REGION_RECTS(prgnDst);
    ppt  = pptSrc;

    if (dy > 0 || (dy == 0 && dx > 0))
    {
        /* Moving down or right: process boxes in reverse order */
        pbox += nbox - 1;
        ppt  += nbox - 1;
        for (i = 0; i < nbox; i++, pbox--, ppt--)
        {
            copyVRAMBox(pRTG->frameBase, pRTG->fbBase, VA2000_STRIDE,
                        ppt->x,  ppt->y,
                        pbox->x2 - pbox->x1,
                        pbox->y2 - pbox->y1,
                        pbox->x1, pbox->y1);
        }
    }
    else
    {
        for (i = 0; i < nbox; i++, pbox++, ppt++)
        {
            copyVRAMBox(pRTG->frameBase, pRTG->fbBase, VA2000_STRIDE,
                        ppt->x,  ppt->y,
                        pbox->x2 - pbox->x1,
                        pbox->y2 - pbox->y1,
                        pbox->x1, pbox->y1);
        }
    }

    DEALLOCATE_LOCAL(pptSrc);
    (*pScreen->RegionDestroy)(prgnDst);
}

/* ---------------------------------------------------------------------- */
/* Backing store                                                           */

/*
** va2000SaveAreas / va2000RestoreAreas — the two functions mibstore needs.
**
** Coordinate conventions are mibstore's, taken from cfbbstore.c: the region
** handed to SaveAreas is pixmap-relative and (xorg,yorg) takes a box to its
** place on screen, while the region handed to RestoreAreas is
** screen-relative and the same offset comes back off.  Getting those two
** the same way round is the whole difficulty.
**
** Both are box copies between VRAM and host memory, which is the same thing
** va2000CopyRects does, so they go through the same run copier.
*/
void
va2000SaveAreas(pPixmap, prgnSave, xorg, yorg)
PixmapPtr pPixmap;
RegionPtr prgnSave;
int       xorg, yorg;
{
    rtgScreenPtr    pRTG = GetRTGScreen(pPixmap->drawable.pScreen);
    unsigned short *sbase = pRTG->fbBase;
    unsigned short *dbase = (unsigned short *) pPixmap->devPrivate.ptr;
    int             sstride = VA2000_STRIDE;
    int             dstride = (int) pPixmap->devKind / 2;
    int             pw = (int) pPixmap->drawable.width;
    int             ph = (int) pPixmap->drawable.height;
    BoxPtr          pbox = REGION_RECTS(prgnSave);
    int             n = REGION_NUM_RECTS(prgnSave);
    int             x1, y1, x2, y2, y;

    while (n--)
    {
        x1 = max(pbox->x1, 0);
        y1 = max(pbox->y1, 0);
        x2 = min((int) pbox->x2, pw);
        y2 = min((int) pbox->y2, ph);
        pbox++;

        for (y = y1; y < y2; y++)
            va2000CopyRun(dbase + y * dstride + x1,
                          sbase + (y + yorg) * sstride + (x1 + xorg),
                          x2 - x1);
    }
}

void
va2000RestoreAreas(pPixmap, prgnRestore, xorg, yorg)
PixmapPtr pPixmap;
RegionPtr prgnRestore;
int       xorg, yorg;
{
    rtgScreenPtr    pRTG = GetRTGScreen(pPixmap->drawable.pScreen);
    unsigned short *dbase = pRTG->fbBase;
    unsigned short *sbase = (unsigned short *) pPixmap->devPrivate.ptr;
    int             dstride = VA2000_STRIDE;
    int             sstride = (int) pPixmap->devKind / 2;
    int             pw = (int) pPixmap->drawable.width;
    int             ph = (int) pPixmap->drawable.height;
    BoxPtr          pbox = REGION_RECTS(prgnRestore);
    int             n = REGION_NUM_RECTS(prgnRestore);
    int             x1, y1, x2, y2, y, sx, sy;

    while (n--)
    {
        x1 = pbox->x1;
        y1 = pbox->y1;
        x2 = pbox->x2;
        y2 = pbox->y2;
        pbox++;

        /* Clamp against the pixmap, in pixmap coordinates. */
        if (x1 - xorg < 0)   x1 = xorg;
        if (y1 - yorg < 0)   y1 = yorg;
        if (x2 - xorg > pw)  x2 = pw + xorg;
        if (y2 - yorg > ph)  y2 = ph + yorg;

        for (y = y1; y < y2; y++)
        {
            sy = y - yorg;
            sx = x1 - xorg;
            va2000CopyRun(dbase + y * dstride + x1,
                          sbase + sy * sstride + sx,
                          x2 - x1);
        }
    }
}

/*
** sizeBucket -- which histogram slot an area of this many pixels falls in.
**
** Bucket 0 is under 64 pixels, and each one after that is four times the
** last: 64, 256, 1K, 4K, 16K, 64K, 256K and above.  Four is a coarse step
** on purpose -- the question is which order of magnitude pixmap copies
** land in, not their exact distribution.
*/
static int
sizeBucket(area)
long area;
{
    int b = 0;

    area >>= 6;
    while (area > 0 && b < VA2000_SZBUCKETS - 1)
    {
        area >>= 2;
        b++;
    }
    return b;
}

/* ---------------------------------------------------------------------- */
/* va2000CopyRects — rectangle copy where at least one end is a pixmap      */

long va2000_copy_fast = 0;
long va2000_copy_slow = 0;

/*
** Copy width x height pixels from one drawable to another, row by row,
** clipped against the GC's composite clip.
**
** Either end may be a window (VRAM) or a pixmap (host memory); drawBase and
** drawStride already tell the two apart.  The source rectangle is clamped
** to the source drawable first, so a copy that reaches past the edge of a
** pixmap does not read outside the allocation -- X leaves the contents
** undefined there, not the server's memory safety.
**
** Source and destination can be the same memory: miDC scrolls the area it
** saved from under the cursor by copying the save pixmap onto itself.  The
** loop below picks its direction for that.
*/

/*
** va2000CopyRunRev -- copy n pixels from the far end backwards.
**
** For an overlapping copy that moves pixels right within one row.  Word at
** a time: this is the rare direction, not worth the longword treatment
** va2000CopyRun gets.
*/
static void
va2000CopyRunRev(dst, src, n)
register unsigned short *dst;
register unsigned short *src;
register int             n;
{
    dst += n;
    src += n;
    while (n-- > 0)
        *--dst = *--src;
}

static void
va2000CopyRects(pSrcDraw, pDstDraw, pGC, srcx, srcy, width, height, dstx, dsty)
DrawablePtr pSrcDraw;
DrawablePtr pDstDraw;
GCPtr       pGC;
int         srcx, srcy;
int         width, height;
int         dstx, dsty;
{
    rtgScreenPtr    pRTG = GetRTGScreen(pDstDraw->pScreen);
    unsigned short *sbase, *dbase;
    int             sstride, dstride;
    RegionPtr       clip = rtgGCClip(pGC);
    BoxPtr          pbox;
    int             nbox;
    int             absSrcX, absSrcY, absDstX, absDstY;
    int             srcLimX, srcLimY;
    int             cx1, cy1, cx2, cy2, yy;
    int             sameMem;

    sbase   = drawBase(pSrcDraw, pRTG);
    sstride = drawStride(pSrcDraw);
    dbase   = drawBase(pDstDraw, pRTG);
    dstride = drawStride(pDstDraw);
    sameMem = (sbase == dbase && sstride == dstride);

    absSrcX = pSrcDraw->x + srcx;
    absSrcY = pSrcDraw->y + srcy;
    absDstX = pDstDraw->x + dstx;
    absDstY = pDstDraw->y + dsty;

    /*
    ** Clamp the source rectangle to the source drawable, BOTH edges, moving
    ** the destination with it.
    **
    ** A caller may legitimately ask for a rectangle that hangs off the edge:
    ** miDC does exactly that every time the pointer is near one, because it
    ** saves the area under the cursor without checking whether all of it
    ** exists.  X says the pixels outside are undefined.  It does not say
    ** they are the server's to go and read.
    **
    ** The first version of this clamped only the far edge, and the near one
    ** cost a hardware bus error: the server read 716 bytes before the
    ** framebuffer -- y = -1, x = 922 -- at an address the FPGA does not
    ** decode.  It had been reading and writing just outside the framebuffer
    ** for a while before that, which is where the fragments left behind in
    ** title bars came from; the fixed 2 MB mapping had been absorbing it.
    */
    srcLimX = pSrcDraw->x + (int) pSrcDraw->width;
    srcLimY = pSrcDraw->y + (int) pSrcDraw->height;

    if (absSrcX < pSrcDraw->x)
    {
        int d = pSrcDraw->x - absSrcX;
        absSrcX += d;
        absDstX += d;
        width   -= d;
    }
    if (absSrcY < pSrcDraw->y)
    {
        int d = pSrcDraw->y - absSrcY;
        absSrcY += d;
        absDstY += d;
        height  -= d;
    }
    if (absSrcX + width > srcLimX)
        width = srcLimX - absSrcX;
    if (absSrcY + height > srcLimY)
        height = srcLimY - absSrcY;
    if (width <= 0 || height <= 0)
        return;

    nbox = REGION_NUM_RECTS(clip);
    pbox = REGION_RECTS(clip);

    while (nbox--)
    {
        cx1 = max(absDstX,          pbox->x1);
        cy1 = max(absDstY,          pbox->y1);
        cx2 = min(absDstX + width,  pbox->x2);
        cy2 = min(absDstY + height, pbox->y2);
        pbox++;

        if (cx1 >= cx2 || cy1 >= cy2)
            continue;

        /*
        ** Overlap.  miDCChangeSave scrolls the pixels it saved from under
        ** the cursor by doing CopyArea(pSave, pSave, ...) with the
        ** destination offset by however far the pointer moved, so source
        ** and destination are the same memory and they overlap.  Copying
        ** rows top-down and each row left-to-right then reads back what it
        ** has already written, and smears the leading pixels across the
        ** whole rectangle.  Over a plain background nothing shows; over a
        ** title bar that smear is the debris the cursor leaves behind.
        **
        ** miCopyArea, which this fast path replaced, chose the direction.
        ** So does copyVRAMBox for window-to-window.  This is the third
        ** place that has to.
        **
        ** Rows moving down are copied bottom-up.  Rows staying level but
        ** moving right are copied right-to-left.  Everything else -- a
        ** different drawable, or a move up or left -- is safe forwards.
        */
        if (sameMem && absDstY > absSrcY)
        {
            for (yy = cy2 - 1; yy >= cy1; yy--)
                va2000CopyRun(dbase + yy * dstride + cx1,
                              sbase + (absSrcY + (yy - absDstY)) * sstride
                                    + (absSrcX + (cx1 - absDstX)),
                              cx2 - cx1);
        }
        else if (sameMem && absDstY == absSrcY && absDstX > absSrcX)
        {
            for (yy = cy1; yy < cy2; yy++)
                va2000CopyRunRev(dbase + yy * dstride + cx1,
                                 sbase + (absSrcY + (yy - absDstY)) * sstride
                                       + (absSrcX + (cx1 - absDstX)),
                                 cx2 - cx1);
        }
        else
        {
            for (yy = cy1; yy < cy2; yy++)
                va2000CopyRun(dbase + yy * dstride + cx1,
                              sbase + (absSrcY + (yy - absDstY)) * sstride
                                    + (absSrcX + (cx1 - absDstX)),
                              cx2 - cx1);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* va2000CopyArea                                                          */

/*
** va2000CopyArea — copy a rectangular region between drawables.
**
** Fast path: both drawables are windows on the same screen → direct VRAM
** copy clipped against the GC composite clip.
**
** All other cases (pixmap source or destination) fall back to miCopyArea,
** which uses GetSpans/SetSpans (correct but slower).
**
** Returns NULL (no backing-store damage region).
*/
RegionPtr
va2000CopyArea(pSrcDraw, pDstDraw, pGC, srcx, srcy, width, height, dstx, dsty)
DrawablePtr pSrcDraw;
DrawablePtr pDstDraw;
GCPtr       pGC;
int         srcx, srcy;
int         width, height;
int         dstx, dsty;
{
    rtgScreenPtr   pRTG;
    unsigned short *fbBase;
    RegionPtr       clip;
    BoxPtr          pbox;
    int             nbox;
    int             absDstX, absDstY;
    int             absSrcX, absSrcY;
    int             cx1, cy1, cx2, cy2;
    int             bw, bh;
    int             bsrcx, bsrcy;
    int             dx_move, dy_move;
    int             i;

    if (pSrcDraw->pScreen != pDstDraw->pScreen)
        return miCopyArea(pSrcDraw, pDstDraw, pGC,
                          srcx, srcy, width, height, dstx, dsty);

    /*
    ** Anything involving a pixmap used to go to miCopyArea, which copies a
    ** scanline at a time through the GetSpans and SetSpans function
    ** pointers and the span machinery between them.  Measured here, that is
    ** 32-43 operations per second against 1145-1243 for the window-to-window
    ** blitter path: a factor of thirty, on the path the software cursor
    ** takes on every pointer move, because miDC saves what is under the
    ** cursor to a pixmap and puts it back.
    **
    ** Both ends are just rectangles of 16-bit pixels -- one in VRAM, one in
    ** host memory -- so copy the rows.
    */
    if (pSrcDraw->type != DRAWABLE_WINDOW || pDstDraw->type != DRAWABLE_WINDOW)
    {
        va2000_copy_hist[sizeBucket((long) width * (long) height)]++;

        if (VA2000_OPT(VA2000_OPT_COPY)          &&
            pSrcDraw->depth == VA2000_DEPTH      &&
            pDstDraw->depth == VA2000_DEPTH      &&
            pGC->alu == GXcopy                   &&
            (pGC->planemask & 0xffffUL) == 0xffffUL)
        {
            va2000_copy_fast++;
            va2000CopyRects(pSrcDraw, pDstDraw, pGC,
                            srcx, srcy, width, height, dstx, dsty);
            return miHandleExposures(pSrcDraw, pDstDraw, pGC,
                                     srcx, srcy, width, height,
                                     dstx, dsty, (unsigned long) 0);
        }
        va2000_copy_slow++;
        return miCopyArea(pSrcDraw, pDstDraw, pGC,
                          srcx, srcy, width, height, dstx, dsty);
    }

    pRTG   = GetRTGScreen(pDstDraw->pScreen);
    fbBase = pRTG->fbBase;
    clip   = rtgGCClip(pGC);

    /* Absolute destination rectangle */
    absDstX = pDstDraw->x + dstx;
    absDstY = pDstDraw->y + dsty;

    /* Absolute source origin */
    absSrcX = pSrcDraw->x + srcx;
    absSrcY = pSrcDraw->y + srcy;

    /* Delta from destination to source (source = destination + delta) */
    dx_move = absDstX - absSrcX;
    dy_move = absDstY - absSrcY;

    nbox = REGION_NUM_RECTS(clip);
    pbox = REGION_RECTS(clip);

    /*
    ** Process clip boxes in direction-aware order to handle VRAM overlap.
    ** Boxes from REGION_RECTS are sorted top-to-bottom, left-to-right.
    */
    if (dy_move > 0 || (dy_move == 0 && dx_move > 0))
        pbox += nbox - 1;   /* start from last box */

    for (i = 0; i < nbox; i++)
    {
        cx1 = max(absDstX,          pbox->x1);
        cy1 = max(absDstY,          pbox->y1);
        cx2 = min(absDstX + width,  pbox->x2);
        cy2 = min(absDstY + height, pbox->y2);

        if (cx1 < cx2 && cy1 < cy2)
        {
            bsrcx = absSrcX + (cx1 - absDstX);
            bsrcy = absSrcY + (cy1 - absDstY);
            bw    = cx2 - cx1;
            bh    = cy2 - cy1;

            copyVRAMBox(pRTG->frameBase, fbBase, VA2000_STRIDE, bsrcx, bsrcy, bw, bh, cx1, cy1);
        }

        if (dy_move > 0 || (dy_move == 0 && dx_move > 0))
            pbox--;
        else
            pbox++;
    }

    /*
    ** Tell the client which parts of the copy came from somewhere it could
    ** not see.
    **
    ** This returned NULL, which meant a CopyArea whose source was partly
    ** obscured never produced a GraphicsExpose -- so the client never
    ** redrew, and whatever happened to be in that part of VRAM (another
    ** window's contents) was copied instead and stayed.  That is the
    ** leftover-after-a-window-move artefact in README's known issues.
    **
    ** miHandleExposures works out the region and either returns it or posts
    ** the events itself, depending on the GC.  cfb and mi both end their
    ** CopyArea this way.
    */
    return miHandleExposures(pSrcDraw, pDstDraw, pGC,
                             srcx, srcy, width, height,
                             dstx, dsty, (unsigned long) 0);
}
