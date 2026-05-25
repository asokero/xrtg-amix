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

#define BLITWAIT(base) \
    while (VA2000_READREG(base, VA2000_BLT_ENABLE) != 0) ;

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
*/
static void
fillRun(dst, pixel, w)
unsigned short *dst;
unsigned short  pixel;
int             w;
{
    while (w-- > 0)
        *dst++ = pixel;
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
        case GXcopy:         while (w-->0)  *p++ = pixel;              break;
        case GXxor:          while (w-->0) { *p ^= pixel;    p++; }    break;
        case GXor:           while (w-->0) { *p |= pixel;    p++; }    break;
        case GXand:          while (w-->0) { *p &= pixel;    p++; }    break;
        case GXinvert:       while (w-->0) { *p ^= 0xFFFF;  p++; }    break;
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
    BoxPtr          pbox;
    int             nbox;
    unsigned short *psrc;
    int             i, cx1, cx2, sw;

    psrc = (unsigned short *) pcharsrc;

    for (i = 0; i < nspans; i++, ppt++, pwidths++)
    {
        sw = *pwidths;
        if (sw <= 0)
        {
            psrc += (sw < 0 ? -sw : 0);
            continue;
        }

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
                memcpy((char *)(base + ppt->y * stride + cx1),
                       (char *)(psrc + (cx1 - ppt->x)),
                       (size_t)((cx2 - cx1) * 2));
            pbox++;
        }
        psrc += sw;
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
        if (w <= 0)
            continue;
        memcpy((char *)dst,
               (char *)(base + ppt->y * stride + ppt->x),
               (size_t)(w * 2));
        dst += w;
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
*/
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
    int             useHW  = (pDraw->type == DRAWABLE_WINDOW &&
                              pGC->alu == GXcopy);
    BoxPtr          pbox;
    int             nbox;
    int             xorg   = pDraw->x;
    int             yorg   = pDraw->y;
    int             rx1, ry1, rx2, ry2;
    int             cx1, cy1, cx2, cy2;
    int             y, w, h;

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
                if (useHW)
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
                        fillRun(base + y * stride + cx1, pixel, w);
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
void
va2000PaintWindow(pWin, prgn, what)
WindowPtr pWin;
RegionPtr prgn;
int       what;
{
    rtgScreenPtr    pRTG   = GetRTGScreen(pWin->drawable.pScreen);
    unsigned short  pixel;
    BoxPtr          pbox;
    int             nbox;

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
            if (pbox->x1 < pbox->x2 && pbox->y1 < pbox->y2)
                blitFill(regBase,
                         (unsigned short) pbox->x1,
                         (unsigned short) pbox->y1,
                         (unsigned short)(pbox->x2 - 1),
                         (unsigned short)(pbox->y2 - 1),
                         pixel);
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

    /* Only the window-to-window same-screen path gets the fast VRAM blit */
    if (pSrcDraw->type != DRAWABLE_WINDOW ||
        pDstDraw->type != DRAWABLE_WINDOW ||
        pSrcDraw->pScreen != pDstDraw->pScreen)
    {
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

    return (RegionPtr) NULL;
}
