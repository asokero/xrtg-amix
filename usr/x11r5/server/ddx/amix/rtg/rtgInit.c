/*
** rtgInit.c — RTG DDX entry points for xamix-rtg.
**
** Provides rtgProbe() and rtgCreate() which plug into amixFbData[] in
** amixInit.c, plus the static AddScreen callback rtgScreenInit().
**
** The amixFbData[] entry for RTG:
**   #ifdef RTG
**       rtgProbe, rtgCreate,
**   #endif
**
** rtgProbe() checks that a supported RTG card is present and fills in
** amixFbs[].  Currently only VA2000 is supported; va2000Probe() does
** the actual hardware detection.
**
** rtgCreate() registers pixmap formats for 16-bit TrueColor and queues
** the screen for AddScreen().
**
** rtgScreenInit() is the AddScreen callback.  It allocates the
** rtgScreenRec, calls va2000InitHW() to open the device and write the
** mode register sequence, then calls va2000ScreenInit() to install the
** drawing function vector into the ScreenRec.
*/

#include "../amix.h"
#include "resource.h"
#include "pixmapstr.h"
#include "servermd.h"
#include <scrnintstr.h>
#include "mipointer.h"
#include "mi.h"
#include "rtg.h"
#include "va2000/va2000.h"

extern int  XmonitorResolution;
extern int  YmonitorResolution;
extern unsigned long amixGeneration;
extern fbFd amixFbs[];
extern miPointerScreenFuncRec amixPointerScreenFuncs;
extern void amixBlockHandler();
extern void amixWakeupHandler();
extern Bool amixCursorInitialize();

/* forward — defined in va2000/va2000screen.c */
extern Bool va2000ScreenInit();
extern Bool va2000CreateDefColormap();

/* mfb — needed to initialise depth-1 GC private storage */
extern Bool mfbAllocatePrivates();

/* ------------------------------------------------------------------ */
/* Globals defined here, declared extern in rtg.h                     */

int rtgScreenIndex    = -1;
int rtgGCPrivateIndex = -1;

/* ------------------------------------------------------------------ */
/* TrueColor 16-bit visual for RGB565                                  */

static VisualRec rtgVisual;     /* filled in rtgCreate() each generation */
static VisualID  rtgVID;
static DepthRec  rtgDepth;

static unsigned long rtgGeneration = 0;

/* ------------------------------------------------------------------ */

static Bool
rtgSaveScreen(pScreen, on)
ScreenPtr pScreen;
int       on;
{
    if (on != SCREEN_SAVER_ON)
    {
        SetTimeSinceLastInputEvent();
        return TRUE;
    }
    return FALSE;
}

static Bool
rtgCloseScreen(index, pScreen)
int       index;
ScreenPtr pScreen;
{
    rtgScreenPtr pRTG = GetRTGScreen(pScreen);
    Bool         ret;

    pScreen->CloseScreen = pRTG->CloseScreen;
    ret = (*pScreen->CloseScreen)(index, pScreen);
    (*pScreen->SaveScreen)(pScreen, SCREEN_SAVER_OFF);

    va2000CloseHW(pRTG);
    xfree((pointer) pRTG);

    return ret;
}

/* ------------------------------------------------------------------ */

/*
** rtgScreenInit — AddScreen callback, called once per screen by DIX.
**
** Allocates the rtgScreenRec, initialises hardware via va2000InitHW(),
** installs the drawing function vector via va2000ScreenInit(), then
** wires up CloseScreen, SaveScreen, block handlers and cursor.
*/
static Bool
rtgScreenInit(index, pScreen, argc, argv)
int       index;
ScreenPtr pScreen;
int       argc;
char    **argv;
{
    rtgScreenPtr pRTG;

    ErrorF("rtgScreenInit: index=%d\n", index);

    pRTG = (rtgScreenPtr) xalloc(sizeof(rtgScreenRec));
    if (!pRTG)
    {
        ErrorF("rtgScreenInit: xalloc pRTG failed\n");
        return FALSE;
    }
    memset((char *) pRTG, 0, sizeof(rtgScreenRec));

    pScreen->devPrivates[rtgScreenIndex].ptr = (pointer) pRTG;

    /* Static geometry for VA2000 800x600x16 */
    pRTG->width        = VA2000_WIDTH;
    pRTG->height       = VA2000_HEIGHT;
    pRTG->pitch        = VA2000_PITCH;
    pRTG->bitsPerPixel = VA2000_BPP;
    pRTG->depth        = VA2000_DEPTH;
    pRTG->fbOffset     = VA2000_FB_OFFSET;

    ErrorF("rtgScreenInit: geometry set, rtgGCPrivateIndex=%d\n", rtgGCPrivateIndex);

    /* mfb GC private storage — required so depth-1 GCs can be delegated to
       mfbCreateGC (same pattern as cfb/cfbscrinit.c:mfbAllocatePrivates) */
    if (!mfbAllocatePrivates(pScreen, (int *)NULL, (int *)NULL))
    {
        ErrorF("rtgScreenInit: mfbAllocatePrivates failed\n");
        xfree((pointer) pRTG);
        return FALSE;
    }

    /* GC private storage for this screen */
    if (!AllocateGCPrivate(pScreen, rtgGCPrivateIndex, sizeof(RTGGCPRIV)))
    {
        ErrorF("rtgScreenInit: AllocateGCPrivate failed\n");
        xfree((pointer) pRTG);
        return FALSE;
    }

    ErrorF("rtgScreenInit: AllocateGCPrivate ok, calling va2000InitHW\n");

    /* Open /dev/va2000, mmap, write init register sequence */
    if (!va2000InitHW(pRTG))
    {
        ErrorF("rtgScreenInit: va2000InitHW failed\n");
        xfree((pointer) pRTG);
        return FALSE;
    }

    ErrorF("rtgScreenInit: hw init done, calling va2000ScreenInit\n");

    /*
    ** Install the ScreenRec function vector for VA2000.
    ** va2000ScreenInit() also sets pScreen->numVisuals, visuals, depths,
    ** rootDepth, rootVisual, defColormap, blackPixel, whitePixel.
    */
    if (!va2000ScreenInit(pScreen, pRTG,
                          XmonitorResolution, YmonitorResolution,
                          &rtgVisual, &rtgDepth))
    {
        ErrorF("rtgScreenInit: va2000ScreenInit failed\n");
        va2000CloseHW(pRTG);
        xfree((pointer) pRTG);
        return FALSE;
    }

    ErrorF("rtgScreenInit: screen init done, chaining CloseScreen\n");

    /* Chain CloseScreen and install RTG cleanup */
    pRTG->CloseScreen   = pScreen->CloseScreen;
    pScreen->CloseScreen = rtgCloseScreen;
    pScreen->SaveScreen  = rtgSaveScreen;

    /* Autorepeat handlers */
    pScreen->BlockHandler  = amixBlockHandler;
    pScreen->WakeupHandler = amixWakeupHandler;
    pScreen->blockData     = (pointer) NULL;
    pScreen->wakeupData    = (pointer) NULL;

    ErrorF("rtgScreenInit: calling amixCursorInitialize\n");

    /* Software cursor — VA2000 has no hardware sprite */
    if (!amixCursorInitialize(pScreen))
    {
        ErrorF("rtgScreenInit: amixCursorInitialize returned FALSE, using miDC\n");
        miDCInitialize(pScreen, &amixPointerScreenFuncs);
    }

    ErrorF("rtgScreenInit: cursor done, calling va2000CreateDefColormap\n");

    (void) rtgSaveScreen(pScreen, SCREEN_SAVER_FORCER);

    if (!va2000CreateDefColormap(pScreen))
    {
        ErrorF("rtgScreenInit: va2000CreateDefColormap failed\n");
        return FALSE;
    }

    ErrorF("rtgScreenInit: done, returning TRUE\n");
    return TRUE;
}

/* ------------------------------------------------------------------ */

/*
** rtgProbe — probeProc entry in amixFbData[].
**
** Called by InitOutput() to verify an RTG card is accessible.
** Fills in the amixFbs[index] entry with screen geometry so that
** the create phase can proceed.
**
** Input handling note: unlike the TIGA path which calls amixMonoProbe()
** to open the native AMIX screen device and thereby obtain keyboard and
** mouse file descriptors, RTG relies on the Amiga's native input
** hardware exposed via the screen device at amixFbs[index].fd.  For now
** the caller (a modified InitOutput or a -rtg command-line option) is
** responsible for ensuring amixFbs[index].fd is valid before drawing
** begins.  amixFbs[index].fd = -1 is set here as a safe sentinel.
*/
Bool
rtgProbe(pScreenInfo, index, fbNum, argc, argv)
ScreenInfo *pScreenInfo;
int         index;
int         fbNum;
int         argc;
char      **argv;
{
    ErrorF("rtgProbe: index=%d\n", index);

    if (!va2000Probe())
    {
        ErrorF("rtgProbe: RTG card not found at %s\n", VA2000_DEV);
        return FALSE;
    }

    amixFbs[index].mapped    = TRUE;
    amixFbs[index].fd        = -1;       /* input fd: see note above        */
    amixFbs[index].bp.width  = VA2000_WIDTH;
    amixFbs[index].bp.height = VA2000_HEIGHT;

    ErrorF("rtgProbe: ok, width=%d height=%d\n", VA2000_WIDTH, VA2000_HEIGHT);
    return TRUE;
}

/*
** rtgCreate — createProc entry in amixFbData[].
**
** Called by InitOutput() after a successful rtgProbe().  Allocates the
** per-generation screen and GC private indices, initialises the
** TrueColor VisualRec for RGB565, sets the 16-bit pixmap format, and
** registers the screen with the DIX layer via AddScreen().
*/
Bool
rtgCreate(pScreenInfo, argc, argv)
ScreenInfo *pScreenInfo;
int         argc;
char      **argv;
{
    ErrorF("rtgCreate: called, serverGeneration=%d\n", serverGeneration);

    if (rtgGeneration != serverGeneration)
    {
        rtgScreenIndex = AllocateScreenPrivateIndex();
        ErrorF("rtgCreate: rtgScreenIndex=%d\n", rtgScreenIndex);
        if (rtgScreenIndex < 0)
        {
            ErrorF("rtgCreate: AllocateScreenPrivateIndex failed\n");
            return FALSE;
        }

        rtgGCPrivateIndex = AllocateGCPrivateIndex();
        ErrorF("rtgCreate: rtgGCPrivateIndex=%d\n", rtgGCPrivateIndex);
        if (rtgGCPrivateIndex < 0)
        {
            ErrorF("rtgCreate: AllocateGCPrivateIndex failed\n");
            return FALSE;
        }

        /* TrueColor visual for 16-bit RGB565 */
        rtgVisual.vid            = FakeClientID(0);
        rtgVisual.class          = TrueColor;
        rtgVisual.bitsPerRGBValue = 8;
        rtgVisual.ColormapEntries = 64;   /* 2^6 — green channel dominates */
        rtgVisual.nplanes        = VA2000_DEPTH;
        rtgVisual.redMask        = VA2000_RED_MASK;
        rtgVisual.greenMask      = VA2000_GREEN_MASK;
        rtgVisual.blueMask       = VA2000_BLUE_MASK;
        rtgVisual.offsetRed      = VA2000_RED_SHIFT;
        rtgVisual.offsetGreen    = VA2000_GREEN_SHIFT;
        rtgVisual.offsetBlue     = VA2000_BLUE_SHIFT;

        rtgVID             = rtgVisual.vid;
        rtgDepth.depth     = VA2000_DEPTH;
        rtgDepth.numVids   = 1;
        rtgDepth.vids      = &rtgVID;

        rtgGeneration = serverGeneration;
    }

    pScreenInfo->numPixmapFormats = 2;

    pScreenInfo->formats[0].depth        = VA2000_DEPTH;
    pScreenInfo->formats[0].bitsPerPixel = VA2000_BPP;
    pScreenInfo->formats[0].scanlinePad  = BITMAP_SCANLINE_PAD;

    pScreenInfo->formats[1].depth        = 1;
    pScreenInfo->formats[1].bitsPerPixel = 1;
    pScreenInfo->formats[1].scanlinePad  = BITMAP_SCANLINE_PAD;

    ErrorF("rtgCreate: calling AddScreen\n");
    return (AddScreen(rtgScreenInit, argc, argv) >= 0);
}
