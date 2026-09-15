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

/* input device globals from amixKbd.c / amixMouse.c / amixMono.c */
extern KbPrivRec  sysKbPriv;
extern PtrPrivRec sysMousePriv;
extern int        amixCurrentScreenIndex;
extern struct scrtype DefaultScrType;

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

/*
** rtgWakeupHandler — fallback no-op wakeup handler.
**
** Used only when rtgProbe() failed to open the AMIX screen device
** (no input available).  amixWakeupHandler calls amixFindInputScreen
** which does FD_ISSET(amixFbs[nscreen].fd, pReadmask); with fd=-1
** that is undefined behaviour and crashes on m68k SVR4.
*/
static void
rtgWakeupHandler(nscreen, pbdata, err, pReadmask)
int           nscreen;
pointer       pbdata;
unsigned long err;
pointer       pReadmask;
{
    (void)nscreen; (void)pbdata; (void)err; (void)pReadmask;
}

/* ------------------------------------------------------------------ */
/* Stall watch                                                          */

/*
** rtgWakeupWrapper / rtgBlockHandler — time what the server spends awake.
**
** A tester reports the desktop stopping for a moment and then carrying on,
** and the benchmark's own numbers agree: consecutive runs of an unchanged
** binary differ by about a factor of two, and the slow ones are 1.6 to 2.7
** seconds longer -- one event of about the length being described, landing
** inside the measurement or missing it.  Whether those are the same thing
** is the question this answers.
**
** The first version of this measured the gap between one block handler
** call and the next, which was wrong and said so loudly: it reported ten
** seconds of "stall" on an idle server, because the gap between two blocks
** is mostly time spent waiting in select() for a client to say something.
**
** The wakeup handler runs when select returns and the block handler runs
** when the server is about to wait again, so the interval between them is
** time spent processing -- which is the thing a freeze would be made of.
** Long processing can be legitimate (one enormous PutImage), so the number
** is reported rather than judged.
*/
long rtg_stalls = 0;            /* awake stretches over RTG_STALL_MS   */
long rtg_stall_worst = 0;       /* the longest, in ms                  */

#define RTG_STALL_MS      200
#define RTG_STALL_LOG     16

static struct timeval rtgWokeAt = { 0, 0 };
static void (*rtgRealWakeup)() = (void (*)()) 0;

/*
** What the driver had done when the last waking began.
**
** A long stretch awake is only half a diagnosis: it says the server did not
** get back to select(), not what it was doing.  Snapshotting the work
** counters at each waking and reporting the difference turns "5450 ms awake"
** into a statement about whether this driver was involved at all.  A stretch
** that shows near-zero drawing was spent somewhere else -- a font read off
** the disk, a server reset, the kernel -- and that is worth establishing
** before optimising any part of the drawing.
*/
static long rtgSnapBlits;
static long rtgSnapPolls;
static long rtgSnapFillHw;
static long rtgSnapFillCpu;
static long rtgSnapCopyHw;
static long rtgSnapCopyCpu;
static long rtgSnapImagePx;
static long rtgSnapGlyphPx;

static void
rtgWakeupWrapper(nscreen, pbdata, err, pReadmask)
int           nscreen;
pointer       pbdata;
unsigned long err;
pointer       pReadmask;
{
    if (VA2000_OPT(VA2000_OPT_STALLWATCH))
    {
        gettimeofday(&rtgWokeAt, (struct timezone *) 0);
        rtgSnapBlits   = va2000_blits;
        rtgSnapPolls   = va2000_blit_polls;
        rtgSnapFillHw  = va2000_fill_hw_px;
        rtgSnapFillCpu = va2000_fill_cpu_px;
        rtgSnapCopyHw  = va2000_copy_hw_px;
        rtgSnapCopyCpu = va2000_copy_cpu_px;
        rtgSnapImagePx = va2000_image_px;
        rtgSnapGlyphPx = va2000_glyph_px;
    }

    if (rtgRealWakeup)
        (*rtgRealWakeup)(nscreen, pbdata, err, pReadmask);
}

static void
rtgBlockHandler(nscreen, pbdata, pptv, pReadmask)
int              nscreen;
pointer          pbdata;
struct timeval **pptv;
pointer          pReadmask;
{
    struct timeval now;
    long           ms;
    int            worse;

    if (VA2000_OPT(VA2000_OPT_STALLWATCH) && rtgWokeAt.tv_sec != 0)
    {
        gettimeofday(&now, (struct timezone *) 0);

        ms = (now.tv_sec - rtgWokeAt.tv_sec) * 1000L
           + (now.tv_usec - rtgWokeAt.tv_usec) / 1000L;

        if (ms >= RTG_STALL_MS)
        {
            rtg_stalls++;

            /*
            ** Log the first few, and after that only a new record.
            **
            ** A flat cap loses the interesting one.  Startup alone can spend
            ** several stretches over the threshold -- mode set, VRAM clear,
            ** fonts -- and if those use up the budget, the stall that happens
            ** later while somebody is actually using the machine is counted
            ** and never described.  That later one is the whole point.
            */
            worse = (ms > rtg_stall_worst);
            if (worse)
                rtg_stall_worst = ms;

            if (rtg_stalls <= RTG_STALL_LOG || worse)
                ErrorF("va2000: %d ms awake: %d blits (%d polls), fill %d hw + %d cpu, copy %d hw + %d cpu, image %d, glyph %d px\n",
                       (int) ms,
                       (int)(va2000_blits      - rtgSnapBlits),
                       (int)(va2000_blit_polls - rtgSnapPolls),
                       (int)(va2000_fill_hw_px  - rtgSnapFillHw),
                       (int)(va2000_fill_cpu_px - rtgSnapFillCpu),
                       (int)(va2000_copy_hw_px  - rtgSnapCopyHw),
                       (int)(va2000_copy_cpu_px - rtgSnapCopyCpu),
                       (int)(va2000_image_px   - rtgSnapImagePx),
                       (int)(va2000_glyph_px   - rtgSnapGlyphPx));
        }

        /* Only count each waking once. */
        rtgWokeAt.tv_sec = 0;
    }

    amixBlockHandler(nscreen, pbdata, pptv, pReadmask);
}

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
    int          i = pScreen->myNum;

    pScreen->CloseScreen = pRTG->CloseScreen;
    ret = (*pScreen->CloseScreen)(index, pScreen);
    (*pScreen->SaveScreen)(pScreen, SCREEN_SAVER_OFF);

    if (amixFbs[i].mapped && amixFbs[i].fd >= 0)
    {
        if (CloseScreen(amixFbs[i].fd))
            ErrorF("rtgCloseScreen: CloseScreen(%d) failed\n", amixFbs[i].fd);
        amixFbs[i].fd     = -1;
        amixFbs[i].mapped = FALSE;
    }

    if (rtg_stalls)
        ErrorF("va2000: %d stretches over %d ms awake, longest %d ms\n",
               (int) rtg_stalls, RTG_STALL_MS, (int) rtg_stall_worst);

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

    /* Screen geometry from selected mode */
    pRTG->width        = va2000_selected_mode->w;
    pRTG->height       = va2000_selected_mode->h;
    pRTG->pitch        = va2000_selected_mode->w * 2;
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

    /* Autorepeat handlers; use real amixWakeupHandler when input fd is open */
    pScreen->BlockHandler  = rtgBlockHandler;
    rtgRealWakeup          = amixFbs[index].mapped
                             ? amixWakeupHandler : rtgWakeupHandler;
    pScreen->WakeupHandler = rtgWakeupWrapper;
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
** rtgOpenInput — open the AMIX screen device and take the input.
**
** SIOCACTIVATE (SelectScreen) makes our screen context the active one for
** event delivery without calling DisplayScreen.  DisplayScreen would want
** NewBitmap -- chip RAM for a native bitmap -- and would take over the ECS
** display, which makes the screen manager send SIGHUP to the X server when
** the native console loses its screen.  SIOCACTIVATE selects us as the event
** target without switching the displayed screen group, so the Amiga display
** is unaffected and no spurious SIGHUP is generated.
**
** This has to be redone from scratch on every server generation, and opening
** is the part that is easy to miss.  InitOutput probes once and remembers it
** in amixDevsProbed, so after a reset only rtgCreate runs.  Worse, the screen
** device does not merely go unreferenced: amixCloseScreen calls CloseScreen()
** on the fd and memsets the whole amixFbs entry, so by the next generation
** the descriptor is closed and the record of it is gone.  Re-activating the
** old fd would be re-activating a closed one.
**
** Easy to reach without meaning to.  The X server resets when its last client
** disconnects, so a session that runs xrdb or xsetroot before its first
** long-lived client resets twice before it has finished starting -- and comes
** up with the display working and no keyboard or mouse.
*/
static int
rtgOpenInput(index, who)
int   index;
char *who;
{
    extern char *display;
    char screenname[1024];
    int  fd;

    if (amixFbs[index].scrtype.dispz == 0)
        amixFbs[index].scrtype = DefaultScrType;

    if (sprintf(screenname, "Xrtg :%s.%d", display, index) < 0)
        fd = OpenScreen("Xrtg", &amixFbs[index].scrtype, 0);
    else
        fd = OpenScreen(screenname, &amixFbs[index].scrtype, 0);

    if (fd < 0)
    {
        ErrorF("%s: OpenScreen failed (%s)\n", who, ScreenError());
        amixFbs[index].fd     = -1;
        amixFbs[index].mapped = FALSE;
        return -1;
    }

    if (fcntl(fd, F_SETFL, O_NDELAY) == -1)
        ErrorF("%s: F_SETFL O_NDELAY failed (%s)\n", who, strerror(errno));

    if (ioctl(fd, SIOCACTIVATE, 0))
        ErrorF("%s: SIOCACTIVATE failed (%s)\n", who, strerror(errno));

    if (ioctl(fd, SIOCSETINPUTMODE, SIM_RAWKEY))
        ErrorF("%s: SIOCSETINPUTMODE SIM_RAWKEY failed (%s)\n",
               who, strerror(errno));

    amixFbs[index].fd     = fd;
    amixFbs[index].mapped = TRUE;
    amixFbs[index].group  = ioctl(fd, SIOCGETGROUP, 0);

    sysKbPriv.fd           = fd;
    sysMousePriv.fd        = fd;
    amixCurrentScreenIndex = index;

    ErrorF("%s: input fd=%d group=%d\n", who, fd, amixFbs[index].group);
    return fd;
}

/*
** rtgProbe — probeProc entry in amixFbData[].
**
** Called by InitOutput() to verify an RTG card is accessible.
** Fills in the amixFbs[index] entry with geometry and opens the
** AMIX screen device for keyboard/mouse input.
**
** Input strategy — same as amixMonoProbe but without NewBitmap:
**   OpenScreen()     — allocate an AMIX screen context; returns event fd
**   DisplayScreen()  — make our context active; AMIX routes input here
**   SIOCSETINPUTMODE — request raw keycodes from the keyboard
**
** DisplayScreen does not affect VA2000 VRAM output (the card reads its
** own framebuffer directly).  It only tells the AMIX event system which
** screen context is "front" so keyboard and mouse events are delivered
** to our fd.  This is confirmed by Klaus Burkert's Xsvga which follows
** the same OpenScreen/DisplayScreen path for input while driving the
** SVGA card through a separate /dev/svga* device.
**
** If OpenScreen fails the server starts without input (display-only).
** amixFbs[index].mapped = FALSE keeps amixFindInputScreen safe.
*/
Bool
rtgProbe(pScreenInfo, index, fbNum, argc, argv)
ScreenInfo *pScreenInfo;
int         index;
int         fbNum;
int         argc;
char      **argv;
{
    int fd;

    ErrorF("rtgProbe: index=%d\n", index);

    if (!va2000Probe())
    {
        ErrorF("rtgProbe: RTG card not found at %s\n", VA2000_DEV);
        return FALSE;
    }

    fd = rtgOpenInput(index, "rtgProbe");

    if (fd < 0)
        ErrorF("rtgProbe: starting display-only\n");

    amixFbs[index].bp.width  = va2000_selected_mode->w;
    amixFbs[index].bp.height = va2000_selected_mode->h;

    ErrorF("rtgProbe: ok, width=%d height=%d\n", va2000_selected_mode->w, va2000_selected_mode->h);
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

    /*
    ** Take the input back.  Generation 1 got it from rtgProbe; every later
    ** one has to open the screen device again, because the probe does not
    ** run a second time and amixCloseScreen closed the last one.
    */
    if (serverGeneration > 1)
        (void) rtgOpenInput(amixCurrentScreenIndex >= 0
                            ? amixCurrentScreenIndex : 0,
                            "rtgCreate");

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
