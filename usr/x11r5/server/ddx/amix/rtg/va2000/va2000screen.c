/*
** va2000screen.c — VA2000 ScreenRec initialisation and GC management.
**
** va2000ScreenInit() builds the full ScreenRec function vector.  It is
** called from rtgInit.c:rtgScreenInit() after va2000InitHW() has opened
** the device and written the mode registers.
**
** GC lifecycle functions (CreateGC, ValidateGC, DestroyGC, ChangeClip,
** DestroyClip, CopyClip) are also here, modelled on dmi/tiggc.c but
** with depth=16 and no TIGA-specific tile/stipple hardware.
**
** Drawing ops installed in ValidateGC:
**   FillSolid        → va2000FillSpans / va2000SolidRect  (va2000draw.c)
**   Tile/Stipple     → va2000FillSpans as solid fallback  (TODO proper impls)
**   CopyArea         → va2000CopyArea                     (va2000draw.c)
**   Lines/poly/arcs  → mi layer (miZeroLine, miPolyArc …)
**   Text             → mi layer (miPolyText8, miImageText8 …)
*/

#include <stdio.h>
#include "X.h"
#include "Xmd.h"
#include "scrnintstr.h"
#include "regionstr.h"
#include "windowstr.h"
#include "pixmapstr.h"
#include "gcstruct.h"
#include "resource.h"
#include "colormap.h"
#include "colormapst.h"
#include "servermd.h"
#include "mi.h"
#include "mfb.h"
#include "../../amix.h"
#include "../rtg.h"
#include "va2000.h"

/* ------------------------------------------------------------------ */
/* External mi functions used below */

extern RegionPtr miCopyPlane();
extern RegionPtr miCopyArea();
extern void      miPolyPoint();

/* ------------------------------------------------------------------ */
/* Forward declarations for functions defined later in this file */

void va2000ValidateGC();
void va2000DestroyGC();
void va2000ChangeClip();
void va2000DestroyClip();
void va2000CopyClip();
static void va2000ChangeGC();
static void va2000CopyGC();
static void va2000pixValidateGC();

/* ------------------------------------------------------------------ */
/* Functions implemented in other va2000 source files */

/* va2000draw.c */
extern void      va2000FillSpans();
extern void      va2000SetSpans();
extern RegionPtr va2000CopyArea();
extern void      va2000CopyWindow();
extern void      va2000PaintWindow();
extern void      va2000SolidRect();

/* va2000win.c */
extern Bool va2000CreateWindow();
extern Bool va2000DestroyWindow();
extern Bool va2000PositionWindow();
extern Bool va2000ChangeWindowAttributes();
extern Bool va2000RealizeWindow();
extern Bool va2000UnrealizeWindow();

/* va2000cmap.c */
extern Bool va2000CreateColormap();
extern void va2000DestroyColormap();
extern void va2000InstallColormap();
extern void va2000UninstallColormap();
extern int  va2000ListInstalledColormaps();
extern void va2000StoreColors();
extern void va2000ResolveColor();
extern Bool va2000CreateDefColormap();

/* va2000pix.c */
extern PixmapPtr va2000CreatePixmap();
extern Bool      va2000DestroyPixmap();
extern Bool      va2000RealizeFont();
extern Bool      va2000UnrealizeFont();

/* rtgInit.c */
extern int rtgGCPrivateIndex;

/* ------------------------------------------------------------------ */
/* GCFuncs and GCOps default tables                                   */

static GCFuncs va2000gcFuncs =
{
    va2000ValidateGC,
    va2000ChangeGC,
    va2000CopyGC,
    va2000DestroyGC,
    va2000ChangeClip,
    va2000DestroyClip,
    va2000CopyClip,
};

/*
** Default ops installed at CreateGC time.  ValidateGC may replace
** individual slots based on fill style, line width, etc.
*/
static GCOps va2000gcOps =
{
    va2000FillSpans,    /* FillSpans   */
    va2000SetSpans,     /* SetSpans    */
    miPutImage,         /* PutImage    — mi handles all wire formats    */
    va2000CopyArea,     /* CopyArea    */
    miCopyPlane,        /* CopyPlane   */
    miPolyPoint,        /* PolyPoint   */
    miZeroLine,         /* Polylines   */
    miPolySegment,      /* PolySegment */
    miPolyRectangle,    /* PolyRectangle */
    miPolyArc,          /* PolyArc     */
    miFillPolygon,      /* FillPolygon */
    va2000SolidRect,    /* PolyFillRect */
    miPolyFillArc,      /* PolyFillArc */
    miPolyText8,        /* PolyText8   */
    miPolyText16,       /* PolyText16  */
    miImageText8,       /* ImageText8  */
    miImageText16,      /* ImageText16 */
    miImageGlyphBlt,    /* ImageGlyphBlt */
    miPolyGlyphBlt,     /* PolyGlyphBlt */
    miPushPixels,       /* PushPixels  */
    miMiter,            /* LineHelper  */
};

/* ------------------------------------------------------------------ */
/* Internal GCFuncs helpers (no-ops; state changes handled in Validate) */

static void
va2000ChangeGC(pGC, mask)
GCPtr  pGC;
BITS32 mask;
{
}

static void
va2000CopyGC(pGCSrc, changes, pGCDst)
GCPtr pGCSrc;
Mask  changes;
GCPtr pGCDst;
{
}

/* ------------------------------------------------------------------ */
/* GC lifecycle                                                        */

/*
** va2000CreateGC — allocate GCFuncs, GCOps and private clip region.
*/
Bool
va2000CreateGC(pGC)
GCPtr pGC;
{
    RTGGCPRIV *pPriv;

    ErrorF("va2000CreateGC: depth=%d\n", pGC->depth);

    /* Delegate 1-bit GCs to mfb — same pattern as cfb/cfbgc.c line 241.
       mfbAllocatePrivates() in rtgScreenInit ensures the mfb GC private
       slot exists before any GC is created. */
    if (pGC->depth == 1)
        return mfbCreateGC(pGC);

    if (pGC->depth != VA2000_DEPTH)
    {
        ErrorF("va2000CreateGC: unsupported depth %d\n", pGC->depth);
        return FALSE;
    }

    ErrorF("va2000CreateGC: pPriv=%p\n", pGC->devPrivates[rtgGCPrivateIndex].ptr);

    pGC->funcs = (GCFuncs *) xalloc(sizeof(GCFuncs));
    if (!pGC->funcs)
        return FALSE;

    pGC->ops = (GCOps *) xalloc(sizeof(GCOps));
    if (!pGC->ops)
    {
        xfree((pointer) pGC->funcs);
        pGC->funcs = NULL;
        return FALSE;
    }

    *pGC->funcs = va2000gcFuncs;
    *pGC->ops   = va2000gcOps;

    pGC->clientClip     = NULL;
    pGC->clientClipType = CT_NONE;
    pGC->miTranslate    = TRUE;
    pGC->fgPixel        = pGC->pScreen->blackPixel;
    pGC->bgPixel        = pGC->pScreen->whitePixel;
    pGC->alu            = GXcopy;
    pGC->planemask      = ~0;
    pGC->lineWidth      = 0;
    pGC->lineStyle      = LineSolid;
    pGC->capStyle       = CapButt;
    pGC->fillStyle      = FillSolid;
    pGC->joinStyle      = JoinMiter;
    pGC->fillRule       = EvenOddRule;
    pGC->arcMode        = ArcPieSlice;
    pGC->patOrg.x   = pGC->patOrg.y   = 0;
    pGC->clipOrg.x  = pGC->clipOrg.y  = 0;
    pGC->lastWinOrg.x = pGC->lastWinOrg.y = 0;

    pPriv = (RTGGCPRIV *) pGC->devPrivates[rtgGCPrivateIndex].ptr;
    pPriv->clip = (*pGC->pScreen->RegionCreate)(NULL, 1);

    return TRUE;
}

/*
** va2000DestroyGC — free GCFuncs, GCOps and private clip region.
*/
void
va2000DestroyGC(pGC)
GCPtr pGC;
{
    RTGGCPRIV *pPriv;

    xfree((pointer) pGC->funcs);
    pGC->funcs = NULL;
    xfree((pointer) pGC->ops);
    pGC->ops = NULL;

    pPriv = (RTGGCPRIV *) pGC->devPrivates[rtgGCPrivateIndex].ptr;
    if (pPriv->clip)
        (*pGC->pScreen->RegionDestroy)(pPriv->clip);
}

/* ------------------------------------------------------------------ */
/* ValidateGC                                                          */

/*
** va2000pixValidateGC — GC for drawing into an off-screen pixmap.
**
** All ops fall back to mi.  va2000FillSpans handles the solid-fill
** case in software (direct pixel writes into host-allocated pixmap data).
*/
static void
va2000pixValidateGC(pGC, change, pDraw)
GCPtr         pGC;
unsigned long change;
DrawablePtr   pDraw;
{
    RTGGCPRIV *pPriv;
    BoxRec     tbox;
    int        win_moved;

    if ((change & (GCClipXOrigin | GCClipYOrigin | GCClipMask)) ||
        (change & GCSubwindowMode) ||
        (pDraw->serialNumber != (pGC->serialNumber & DRAWABLE_SERIAL_BITS)))
    {
        pPriv = (RTGGCPRIV *) pGC->devPrivates[rtgGCPrivateIndex].ptr;
        win_moved = (pGC->lastWinOrg.x != pDraw->x ||
                     pGC->lastWinOrg.y != pDraw->y);

        if (pGC->clientClipType != CT_NONE && win_moved)
        {
            (*pGC->pScreen->RegionCopy)((RegionPtr) pPriv->clip,
                                         (RegionPtr) pGC->clientClip);
            (*pGC->pScreen->TranslateRegion)(pPriv->clip,
                pGC->lastWinOrg.x + pGC->clipOrg.x,
                pGC->lastWinOrg.y + pGC->clipOrg.y);
        }
        else if (pGC->clientClipType == CT_NONE)
        {
            (*pGC->pScreen->RegionDestroy)(pPriv->clip);
            tbox.x1 = 0;           tbox.y1 = 0;
            tbox.x2 = pDraw->width; tbox.y2 = pDraw->height;
            pPriv->clip = (*pGC->pScreen->RegionCreate)(&tbox, 1);
        }
    }

    pGC->ops->FillSpans     = va2000FillSpans;  /* solid; tile/stip: TODO */
    pGC->ops->SetSpans      = va2000SetSpans;
    pGC->ops->PutImage      = miPutImage;
    pGC->ops->CopyArea      = va2000CopyArea;
    pGC->ops->CopyPlane     = miCopyPlane;
    pGC->ops->PolyPoint     = miPolyPoint;
    pGC->ops->Polylines     = miZeroLine;
    pGC->ops->PolySegment   = miPolySegment;
    pGC->ops->PolyRectangle = miPolyRectangle;
    pGC->ops->PolyArc       = miPolyArc;
    pGC->ops->FillPolygon   = miFillPolygon;
    pGC->ops->PolyFillRect  = miPolyFillRect;
    pGC->ops->PolyFillArc   = miPolyFillArc;
    pGC->ops->PolyText8     = miPolyText8;
    pGC->ops->PolyText16    = miPolyText16;
    pGC->ops->ImageText8    = miImageText8;
    pGC->ops->ImageText16   = miImageText16;
    pGC->ops->ImageGlyphBlt = miImageGlyphBlt;
    pGC->ops->PolyGlyphBlt  = miPolyGlyphBlt;
    pGC->ops->PushPixels    = miPushPixels;
    pGC->ops->LineHelper    = miMiter;
}

/*
** va2000ValidateGC — update clip region and select drawing ops.
**
** Called by DIX before any drawing sequence on a new window/GC pair.
** Keeps the cached clip region in GC private up to date, then installs
** the appropriate ops:
**
**   FillSolid  → va2000FillSpans / va2000SolidRect
**   FillTiled  → va2000FillSpans as solid fallback (TODO: real tile ops)
**   Stipple    → va2000FillSpans as solid fallback (TODO: real stip ops)
**   Lines      → miZeroLine (zero-width) / miWideLine (wide)
**   Everything else → mi
*/
void
va2000ValidateGC(pGC, change, pDraw)
GCPtr         pGC;
unsigned long change;
DrawablePtr   pDraw;
{
    RTGGCPRIV *pPriv;
    WindowPtr  pWin;
    int        win_moved;
    RegionPtr  reg;

    if (pDraw->type == DRAWABLE_PIXMAP)
    {
        va2000pixValidateGC(pGC, ~0UL, pDraw);
        return;
    }

    pWin = (WindowPtr) pDraw;
    win_moved = (pGC->lastWinOrg.x != pDraw->x ||
                 pGC->lastWinOrg.y != pDraw->y);
    if (win_moved)
    {
        pGC->lastWinOrg.x = pDraw->x;
        pGC->lastWinOrg.y = pDraw->y;
    }

    if (change & (GCClipXOrigin | GCClipYOrigin | GCClipMask) ||
        change & GCSubwindowMode ||
        pDraw->serialNumber != (pGC->serialNumber & DRAWABLE_SERIAL_BITS) ||
        win_moved)
    {
        pPriv = (RTGGCPRIV *) pGC->devPrivates[rtgGCPrivateIndex].ptr;

        if (pGC->clientClipType != CT_NONE &&
            (change & (GCClipXOrigin | GCClipYOrigin | GCClipMask) || win_moved))
        {
            (*pGC->pScreen->RegionCopy)((RegionPtr) pPriv->clip,
                                         (RegionPtr) pGC->clientClip);
            (*pGC->pScreen->TranslateRegion)(pPriv->clip,
                pGC->lastWinOrg.x + pGC->clipOrg.x,
                pGC->lastWinOrg.y + pGC->clipOrg.y);
        }
        else
        {
            (*pGC->pScreen->RegionCopy)((RegionPtr) pPriv->clip,
                                         &pWin->winSize);
        }

        reg = (pGC->subWindowMode == IncludeInferiors)
              ? NotClippedByChildren(pWin)
              : &pWin->clipList;
        (*pGC->pScreen->Intersect)(pPriv->clip, pPriv->clip, reg);
    }

    /* Line ops */
    if (pGC->lineStyle == LineSolid)
    {
        if (pGC->lineWidth)
        {
            pGC->ops->Polylines   = miWideLine;
            pGC->ops->PolySegment = miPolySegment;
        }
        else
        {
            pGC->ops->Polylines   = miZeroLine;
            pGC->ops->PolySegment = miPolySegment;
        }
    }
    else
    {
        pGC->ops->Polylines   = pGC->lineWidth ? miWideDash : miZeroLine;
        pGC->ops->PolySegment = miPolySegment;
    }

    pGC->ops->LineHelper = (pGC->joinStyle == JoinMiter) ? miMiter : miNotMiter;
    pGC->ops->SetSpans   = va2000SetSpans;

    /* Fill ops — tile/stipple fall back to solid colour (TODO) */
    switch (pGC->fillStyle)
    {
    case FillSolid:
        pGC->ops->FillSpans    = va2000FillSpans;
        pGC->ops->FillPolygon  = miFillPolygon;
        pGC->ops->PolyFillRect = va2000SolidRect;
        pGC->ops->PushPixels   = miPushPixels;
        pGC->ops->PolyText8    = miPolyText8;
        break;

    case FillTiled:
        /* TODO: implement va2000FillTileSpans; using solid as placeholder */
        pGC->ops->FillSpans    = va2000FillSpans;
        pGC->ops->FillPolygon  = miFillPolygon;
        pGC->ops->PolyFillRect = miPolyFillRect;
        pGC->ops->PushPixels   = miPushPixels;
        pGC->ops->PolyText8    = miPolyText8;
        break;

    case FillStippled:
    case FillOpaqueStippled:
        /* TODO: implement va2000FillStipSpans; using solid as placeholder */
        pGC->ops->FillSpans    = va2000FillSpans;
        pGC->ops->FillPolygon  = miFillPolygon;
        pGC->ops->PolyFillRect = miPolyFillRect;
        pGC->ops->PushPixels   = miPushPixels;
        pGC->ops->PolyText8    = miPolyText8;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Clip region management                                              */

/*
** va2000ChangeClip — set a new client clip region on a GC.
*/
void
va2000ChangeClip(pGC, type, pval, nrects)
GCPtr   pGC;
int     type;
pointer pval;
int     nrects;
{
    va2000DestroyClip(pGC);

    if (type == CT_PIXMAP)
    {
        pGC->clientClip = (pointer) mfbPixmapToRegion(pval);
        (*pGC->pScreen->DestroyPixmap)(pval);
    }
    else if (type == CT_REGION)
    {
        pGC->clientClip = pval;
    }
    else if (type != CT_NONE)
    {
        pGC->clientClip = (pointer)
            (*pGC->pScreen->RectsToRegion)(nrects, (xRectangle *) pval, type);
        xfree(pval);
    }
    else
    {
        pGC->clientClipType = CT_NONE;
        pGC->stateChanges  |= GCClipMask;
    }

    pGC->clientClipType =
        (type != CT_NONE && pGC->clientClip) ? CT_REGION : CT_NONE;
    pGC->stateChanges |= GCClipMask;
}

/*
** va2000DestroyClip — free the current client clip region.
*/
void
va2000DestroyClip(pGC)
GCPtr pGC;
{
    if (pGC->clientClipType == CT_PIXMAP)
        (*pGC->pScreen->DestroyPixmap)(pGC->clientClip);
    else if (pGC->clientClipType != CT_NONE)
        (*pGC->pScreen->RegionDestroy)(pGC->clientClip);

    pGC->clientClipType = CT_NONE;
    pGC->clientClip     = NULL;
}

/*
** va2000CopyClip — copy the client clip region from src GC to dst GC.
*/
void
va2000CopyClip(dst, src)
GCPtr dst;
GCPtr src;
{
    RegionPtr rgnCopy;

    if (src->clientClipType == CT_REGION)
    {
        rgnCopy = (*src->pScreen->RegionCreate)(NULL, 1);
        (*src->pScreen->RegionCopy)(rgnCopy, (RegionPtr) src->clientClip);
        va2000ChangeClip(dst, CT_REGION, (pointer) rgnCopy, 0);
        return;
    }
    if (src->clientClipType == CT_PIXMAP)
        ((PixmapPtr) src->clientClip)->refcnt++;
    va2000ChangeClip(dst, src->clientClipType, src->clientClip, 0);
}

/* ------------------------------------------------------------------ */
/* Screen-level functions                                              */

/*
** va2000QueryBestSize — return an acceptable size for cursors, tiles, stipples.
**
** Software cursor can be any size; we cap at 64x64 to match TIGA.
** Tile and stipple: no hardware constraint — return requested size.
*/
void
va2000QueryBestSize(kind, W, H)
int     kind;
CARD16 *W;
CARD16 *H;
{
    switch (kind)
    {
    case CursorShape:
        if (*W > 64) *W = 64;
        if (*H > 64) *H = 64;
        break;

    case TileShape:
    case StippleShape:
        /* No hardware constraint on tile/stipple size */
        break;

    default:
        break;
    }
}

/*
** va2000CloseScreen — minimal hook; hardware cleanup is in rtgCloseScreen.
**
** rtgInit.c:rtgCloseScreen() chains to this, then calls va2000CloseHW().
** This function returns TRUE so the DIX chain continues.
*/
Bool
va2000CloseScreen(i, pScreen)
int       i;
ScreenPtr pScreen;
{
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Screen initialisation                                               */

/*
** va2000ScreenInit — build the complete ScreenRec function vector.
**
** Called from rtgInit.c:rtgScreenInit() after va2000InitHW() has set
** pRTG->fbBase.  pVisual and pDepth are the static TrueColor records
** initialised in rtgInit.c:rtgCreate().
**
** Owns no persistent state beyond what it installs in pScreen.
*/
Bool
va2000ScreenInit(pScreen, pRTG, dpix, dpiy, pVisual, pDepth)
ScreenPtr    pScreen;
rtgScreenPtr pRTG;
int          dpix, dpiy;
VisualRec   *pVisual;
DepthRec    *pDepth;
{
    /* Screen geometry */
    pScreen->width    = pRTG->width;
    pScreen->height   = pRTG->height;
    pScreen->mmWidth  = (pRTG->width  * 254) / (dpix * 10);
    pScreen->mmHeight = (pRTG->height * 254) / (dpiy * 10);

    /* Visuals and depths */
    pScreen->numDepths     = 1;
    pScreen->allowedDepths = pDepth;
    pScreen->rootDepth     = VA2000_DEPTH;
    pScreen->rootVisual    = pVisual->vid;
    pScreen->defColormap   = (Colormap) FakeClientID(0);
    pScreen->minInstalledCmaps = 1;
    pScreen->maxInstalledCmaps = 1;
    pScreen->numVisuals    = 1;
    pScreen->visuals       = pVisual;

    pScreen->backingStoreSupport = NotUseful;
    pScreen->saveUnderSupport    = NotUseful;

    pScreen->blackPixel = 0x0000;   /* RGB565 black */
    pScreen->whitePixel = 0xFFFF;   /* RGB565 white */

    /* Window management — thin hooks; real work done by DIX/mi */
    pScreen->CreateWindow         = va2000CreateWindow;
    pScreen->DestroyWindow        = va2000DestroyWindow;
    pScreen->PositionWindow       = va2000PositionWindow;
    pScreen->ChangeWindowAttributes = va2000ChangeWindowAttributes;
    pScreen->RealizeWindow        = va2000RealizeWindow;
    pScreen->UnrealizeWindow      = va2000UnrealizeWindow;

    /* Font management — no hardware fonts */
    pScreen->RealizeFont   = va2000RealizeFont;
    pScreen->UnrealizeFont = va2000UnrealizeFont;

    /* Screen lifecycle */
    pScreen->CloseScreen      = va2000CloseScreen;
    pScreen->QueryBestSize    = va2000QueryBestSize;
    pScreen->SaveScreen       = (Bool (*)()) NULL; /* installed by rtgInit */
    pScreen->SourceValidate   = (void (*)()) NULL;

    /* Pixel transfer — mi handles format conversion */
    pScreen->GetImage  = miGetImage;
    pScreen->GetSpans  = va2000GetSpans;

    /* GC */
    pScreen->CreateGC      = va2000CreateGC;

    /* Pixmaps */
    pScreen->CreatePixmap  = va2000CreatePixmap;
    pScreen->DestroyPixmap = va2000DestroyPixmap;

    /* Validation */
    pScreen->ValidateTree = miValidateTree;

    /* Colormap — TrueColor: no CLUT; most entries are no-ops */
    pScreen->InstallColormap        = va2000InstallColormap;
    pScreen->UninstallColormap      = va2000UninstallColormap;
    pScreen->ListInstalledColormaps = va2000ListInstalledColormaps;
    pScreen->StoreColors            = va2000StoreColors;
    pScreen->ResolveColor           = va2000ResolveColor;
    pScreen->CreateColormap         = va2000CreateColormap;
    pScreen->DestroyColormap        = va2000DestroyColormap;

    /* Region operations — pure mi */
    pScreen->RegionCreate    = miRegionCreate;
    pScreen->RegionInit      = miRegionInit;
    pScreen->RegionCopy      = miRegionCopy;
    pScreen->RegionDestroy   = miRegionDestroy;
    pScreen->RegionUninit    = miRegionUninit;
    pScreen->Intersect       = miIntersect;
    pScreen->Inverse         = miInverse;
    pScreen->Union           = miUnion;
    pScreen->Subtract        = miSubtract;
    pScreen->RegionReset     = miRegionReset;
    pScreen->TranslateRegion = miTranslateRegion;
    pScreen->RectIn          = miRectIn;
    pScreen->PointInRegion   = miPointInRegion;
    pScreen->RegionNotEmpty  = miRegionNotEmpty;
    pScreen->RegionEmpty     = miRegionEmpty;
    pScreen->RegionExtents   = miRegionExtents;
    pScreen->RegionAppend    = miRegionAppend;
    pScreen->RegionValidate  = miRegionValidate;

    /* Bitmap/region conversion */
    pScreen->BitmapToRegion  = mfbPixmapToRegion; /* 1-bit pixmaps only */
    pScreen->RectsToRegion   = miRectsToRegion;

    /* Window painting — accelerated via va2000draw.c */
    pScreen->PaintWindowBackground = va2000PaintWindow;
    pScreen->PaintWindowBorder     = va2000PaintWindow;
    pScreen->CopyWindow            = va2000CopyWindow;

    /* Exposure and background clearing */
    pScreen->WindowExposures   = miWindowExposures;
    pScreen->ClearToBackground = miClearToBackground;

    /* Miscellaneous */
    pScreen->SendGraphicsExpose = miSendGraphicsExpose;

    /* Block/Wakeup and cursor are installed by rtgInit.c after this returns */

    return TRUE;
}
