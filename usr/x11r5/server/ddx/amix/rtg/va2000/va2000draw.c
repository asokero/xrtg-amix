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

#define VA2000_STRIDE  (VA2000_PITCH / 2)   /* shorts (pixels) per scanline */

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
** Copy a rectangle within the framebuffer, handling overlaps.
** All coordinates are absolute (in pixels); stride is shorts per row.
*/
static void
copyVRAMBox(fbBase, stride, srcx, srcy, w, h, dstx, dsty)
unsigned short *fbBase;
int             stride;
int             srcx, srcy, w, h, dstx, dsty;
{
    unsigned short *src, *dst;
    int             row;

    if (w <= 0 || h <= 0)
        return;

    if (dsty < srcy || (dsty == srcy && dstx <= srcx))
    {
        for (row = 0; row < h; row++)
        {
            src = fbBase + (srcy + row) * stride + srcx;
            dst = fbBase + (dsty + row) * stride + dstx;
            memmove((char *)dst, (char *)src, (size_t)(w * 2));
        }
    }
    else
    {
        for (row = h - 1; row >= 0; row--)
        {
            src = fbBase + (srcy + row) * stride + srcx;
            dst = fbBase + (dsty + row) * stride + dstx;
            memmove((char *)dst, (char *)src, (size_t)(w * 2));
        }
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
    ErrorF("va2000GetImage: pDraw=%p pDraw->pScreen=%p type=%d\n",
           pDraw, pDraw->pScreen, pDraw->type);
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
    unsigned short *base   = drawBase(pDraw, pRTG);
    int             stride = drawStride(pDraw);
    int             i, w;

    nspans = miClipSpans(rtgGCClip(pGC),
                         ppts, pwidths, nspans,
                         ppts, pwidths, fSorted);

    for (i = 0; i < nspans; i++)
    {
        w = pwidths[i];
        if (w > 0)
            fillRun(base + ppts[i].y * stride + ppts[i].x, pixel, w);
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
                w = cx2 - cx1;
                h = cy2 - cy1;
                for (y = cy1; y < cy1 + h; y++)
                    fillRun(base + y * stride + cx1, pixel, w);
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
    unsigned short *base   = pRTG->fbBase;
    BoxPtr          pbox;
    int             nbox;
    int             x, y, w, h;

    ErrorF("va2000PaintWindow: what=%d fbBase=%p\n", what, pRTG ? pRTG->fbBase : 0);

    if (what == PW_BACKGROUND)
    {
        switch (pWin->backgroundState)
        {
        case None:
            return;
        case BackgroundPixel:
            pixel = (unsigned short) pWin->background.pixel;
            break;
        default:
            /* BackgroundPixmap / ParentRelative: not yet implemented */
            return;
        }
    }
    else /* PW_BORDER */
    {
        if (pWin->borderIsPixel)
            pixel = (unsigned short) pWin->border.pixel;
        else
            return;   /* pixmap border: not yet implemented */
    }

    nbox = REGION_NUM_RECTS(prgn);
    pbox = REGION_RECTS(prgn);

    while (nbox--)
    {
        x = pbox->x1;
        y = pbox->y1;
        w = pbox->x2 - pbox->x1;
        h = pbox->y2 - pbox->y1;

        while (h-- > 0)
        {
            fillRun(base + y * VA2000_STRIDE + x, pixel, w);
            y++;
        }
        pbox++;
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
            copyVRAMBox(pRTG->fbBase, VA2000_STRIDE,
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
            copyVRAMBox(pRTG->fbBase, VA2000_STRIDE,
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

            copyVRAMBox(fbBase, VA2000_STRIDE, bsrcx, bsrcy, bw, bh, cx1, cy1);
        }

        if (dy_move > 0 || (dy_move == 0 && dx_move > 0))
            pbox--;
        else
            pbox++;
    }

    return (RegionPtr) NULL;
}
