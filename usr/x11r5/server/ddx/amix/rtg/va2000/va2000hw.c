/*
** va2000hw.c — MNT VA2000 hardware init/close for xamix-rtg.
**
** Three public functions:
**
**   va2000Probe()      — quick open/close of VA2000_DEV to verify presence.
**   va2000InitHW()     — open device, mmap full address space, write the
**                        init register sequence that brings up 800x600x16,
**                        and fill in the rtgScreenRec and va2000ScreenRec.
**   va2000CloseHW()    — restore passthrough, munmap, close.
**
** Register sequence is derived from va2000_test.c (va2000-amix-main).
** Critical registers that were previously missing:
**   0x38/0x3a  Pan pointer — must be zeroed for RTG; left at 0xf8/0x00
**              in passthrough mode.  Without this, the display reads from
**              the wrong part of VRAM despite correct framebuffer writes.
**   0x5c       Pitch shift
**   0x04       Scale mode
**   0x14/0x18/0x1a  Display timing registers
*/

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include "../../amix.h"
#include "../rtg.h"
#include "va2000.h"

/* ======================================================================= */
/* Mode table — timings verified against va2000_restest.c (va2000-amix)   */
/* ======================================================================= */

static VA2000ModeRec va2000_modes[] = {
    /* name         w     h   hss   hse  hmax  vss  vse  vmax  clk */
    { "640x480",   640,  480,  656,  752,  800, 490, 492,  525,  1 },
    { "800x600",   800,  600,  840,  968, 1056, 601, 605,  628,  1 },
    { "1024x768", 1024,  768, 1048, 1184, 1328, 771, 777,  806,  0 },
    { "1280x720", 1280,  720, 1390, 1430, 1650, 725, 730,  750,  0 },
    { "1280x1024",1280, 1024, 1328, 1440, 1600,1025,1028, 1066,  3 },
    { "1920x1080",1920, 1080, 1992, 2000, 2287,1083,1088, 1109,  0 },
    { (char *)0 }
};

VA2000ModePtr va2000_selected_mode = &va2000_modes[1]; /* default: 800x600 */

/* ======================================================================= */
/* Runtime state shared with the rest of the driver                        */
/* ======================================================================= */

VA2000CapsRec va2000_caps = { 0L, VA2000_BUS_UNKNOWN, 0, 0 };

unsigned long va2000_options      = VA2000_OPT_DEFAULT;
int           va2000_blit_min     = VA2000_BLIT_MIN_DEFAULT;
int           va2000_bus_override = VA2000_BUS_UNKNOWN;

/*
** va2000SetMode — select mode by "WxH" string.
** Returns TRUE if found, FALSE if unrecognised (keeps current mode).
*/
Bool
va2000SetMode(name)
char *name;
{
    VA2000ModePtr m;
    for (m = va2000_modes; m->name; m++)
    {
        if (strcmp(m->name, name) == 0)
        {
            va2000_selected_mode = m;
            return TRUE;
        }
    }
    return FALSE;
}

/*
** va2000SetBus — override bus autodetection ("z2" or "z3").
** Returns TRUE if recognised.
*/
Bool
va2000SetBus(name)
char *name;
{
    if (strcmp(name, "z2") == 0)
    {
        va2000_bus_override = VA2000_BUS_Z2;
        return TRUE;
    }
    if (strcmp(name, "z3") == 0)
    {
        va2000_bus_override = VA2000_BUS_Z3;
        return TRUE;
    }
    return FALSE;
}

/*
** va2000BusName — printable name for a VA2000_BUS_* value.
*/
char *
va2000BusName(bus)
int bus;
{
    if (bus == VA2000_BUS_Z2)
        return "Zorro II";
    if (bus == VA2000_BUS_Z3)
        return "Zorro III";
    return "unknown";
}

/*
** va2000ModeBytes — framebuffer bytes the selected mode occupies.
*/
long
va2000ModeBytes()
{
    return (long) va2000_selected_mode->w *
           (long) va2000_selected_mode->h * 2L;
}

/*
** va2000ProbeCaps — ask the driver what board this is.
**
** SVGAIOCGetFBufSize returns the framebuffer size AutoConfig reported,
** which is 4 MB on a Zorro II board and 32 MB on a Zorro III one.  An
** older driver without these ioctls returns -1; then the aperture stays
** unknown and the bus is left at VA2000_BUS_UNKNOWN, which every caller
** must treat as "assume the conservative Zorro II defaults".
*/
static void
va2000ProbeCaps(fd)
int fd;
{
    int v;

    va2000_caps.fbSize    = 0L;
    va2000_caps.bus       = VA2000_BUS_UNKNOWN;
    va2000_caps.busForced = 0;
    va2000_caps.fwVersion = 0;

    v = ioctl(fd, VA2IOC_GETFW, 0);
    if (v > 0)
        va2000_caps.fwVersion = v;

    v = ioctl(fd, SVGAIOCGetFBufSize, 0);
    if (v > 0)
    {
        va2000_caps.fbSize = (long) v;
        va2000_caps.bus    = (va2000_caps.fbSize >= VA2000_Z3_MIN_SIZE)
                             ? VA2000_BUS_Z3 : VA2000_BUS_Z2;
    }

    if (va2000_bus_override != VA2000_BUS_UNKNOWN)
    {
        va2000_caps.bus       = va2000_bus_override;
        va2000_caps.busForced = 1;
    }
}

/*
** va2000Probe — check that VA2000_DEV can be opened for read/write.
** Does not mmap or write any registers.  Returns TRUE if present.
*/
Bool
va2000Probe()
{
    int fd;

    fd = open(VA2000_DEV, O_RDWR);
    if (fd < 0)
        return FALSE;
    (void) close(fd);
    return TRUE;
}

/*
** va2000InitHW — open device, mmap, write mode registers.
**
** Allocates a va2000ScreenRec, opens VA2000_DEV, maps VA2000_MMAP_SIZE
** bytes, then writes the 800x600 modeline and activates RTG mode.
** On success pRTG->frameBase, pRTG->fbBase and pRTG->cardPrivate are
** set.  Returns FALSE and frees all resources on any error.
*/
Bool
va2000InitHW(pRTG)
rtgScreenPtr pRTG;
{
    va2000ScreenPtr  pVA;
    caddr_t          base;
    int              fd;
    long             need;
    long             avail;
    long             mapSize;
    long             minSize;
    long             wantSize;

    ErrorF("va2000InitHW: starting\n");

    pVA = (va2000ScreenPtr) xalloc(sizeof(va2000ScreenRec));
    if (!pVA)
    {
        ErrorF("va2000InitHW: xalloc failed\n");
        return FALSE;
    }

    fd = open(VA2000_DEV, O_RDWR);
    ErrorF("va2000InitHW: fd=%d\n", fd);
    if (fd < 0)
    {
        ErrorF("va2000InitHW: cannot open %s (%s)\n", VA2000_DEV, strerror(errno));
        xfree((pointer) pVA);
        return FALSE;
    }

    /*
    ** Identify the board before touching it.  Everything that differs
    ** between a stock Zorro II A3000UX and a Zorro III machine is derived
    ** from here, and the line below is the first thing to ask for when a
    ** bug report arrives from a machine we cannot reach.
    */
    va2000ProbeCaps(fd);

    ErrorF("va2000: firmware %d, framebuffer %d KB, bus %s%s\n",
           va2000_caps.fwVersion,
           (int)(va2000_caps.fbSize >> 10),
           va2000BusName(va2000_caps.bus),
           va2000_caps.busForced ? " (forced by -bus)" : "");
    ErrorF("va2000: options 0x%04x, blitmin %d\n",
           (int) va2000_options, va2000_blit_min);

    /*
    ** Map what the board actually has, not a fixed 2 MB.
    **
    ** VA2000_MMAP_SIZE was hardcoded to 2 MB, which left 1984 KB for the
    ** framebuffer.  1280x1024 needs 2560 KB and 1920x1080 needs 4050, so
    ** both walked off the end of the mapping in the memset below -- while
    ** the board itself had room for them all along: AutoConfig reports a
    ** 4 MB aperture in Zorro II and 32 MB in Zorro III.
    **
    ** The driver's SVGAIOCGetFBufSize says how much there is.  An older
    ** driver that does not implement it leaves fbSize at 0, and then the
    ** old fixed size is the safe assumption.
    */
    if (va2000_caps.fbSize > 0L)
        mapSize = va2000_caps.fbSize + (long) VA2000_FB_OFFSET;
    else
        mapSize = (long) VA2000_MMAP_SIZE;

    need  = va2000ModeBytes();
    avail = mapSize - (long) VA2000_FB_OFFSET;
    if (need > avail)
    {
        ErrorF("va2000InitHW: mode %s needs %d KB, the board offers %d KB\n",
               va2000_selected_mode->name,
               (int)(need >> 10), (int)(va2000_caps.fbSize >> 10));
        (void) close(fd);
        xfree((pointer) pVA);
        return FALSE;
    }

    /*
    ** Ask for the mode plus a margin, and settle for the mode.
    **
    ** Three sizes have been tried here.  A fixed 2 MB, which was too small
    ** for 1280x1024 and up.  Exactly the mode, which fits every mode but
    ** left no margin, and a drawing path that ran past the end of the
    ** framebuffer became a bus error instead of a harmless scribble.  Then
    ** the whole aperture, which is 32 MB on a Zorro III board and simply
    ** does not fit: mmap returns ENOMEM and the server finds no screens.
    **
    ** So: the mode, plus VA2000_MAP_SLACK, capped at what the board has --
    ** and if even that will not fit, fall back to the mode alone rather
    ** than fail to start.  The margin is insurance, not a load-bearing
    ** part; the overrun it used to hide was a source rectangle read above
    ** row 0, and that one is fixed in va2000CopyRects.
    */
    minSize = need + (long) VA2000_FB_OFFSET;
    wantSize = minSize + (long) VA2000_MAP_SLACK;
    if (wantSize > mapSize)
        wantSize = mapSize;

    mapSize = wantSize;
    base = (caddr_t) mmap(0, (size_t) mapSize,
                          PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == (caddr_t) -1 && wantSize > minSize)
    {
        ErrorF("va2000InitHW: %d KB refused (%s), retrying with %d KB\n",
               (int)(wantSize >> 10), strerror(errno), (int)(minSize >> 10));
        mapSize = minSize;
        base = (caddr_t) mmap(0, (size_t) mapSize,
                              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    }
    ErrorF("va2000InitHW: base=%p, mapped %d KB for a %d KB mode\n",
           base, (int)(mapSize >> 10), (int)(need >> 10));
    if (base == (caddr_t) -1)
    {
        ErrorF("va2000InitHW: mmap failed (%s)\n", strerror(errno));
        (void) close(fd);
        xfree((pointer) pVA);
        return FALSE;
    }

    ErrorF("va2000InitHW: mode %s (%dx%d)\n",
           va2000_selected_mode->name,
           va2000_selected_mode->w,
           va2000_selected_mode->h);

    /*
    ** Write mode registers — sequence from va2000_test.c.
    ** All timing values come from the runtime mode struct.
    */
    VA2000_WRITEREG(base, VA2000_REG_HTOTAL,       va2000_selected_mode->hss);
    VA2000_WRITEREG(base, VA2000_REG_HSYNC_START,  va2000_selected_mode->hse);
    VA2000_WRITEREG(base, VA2000_REG_HSYNC_END,    va2000_selected_mode->hmax);
    VA2000_WRITEREG(base, VA2000_REG_VTOTAL,       va2000_selected_mode->vss);
    VA2000_WRITEREG(base, VA2000_REG_VSYNC_START,  va2000_selected_mode->vse);
    VA2000_WRITEREG(base, VA2000_REG_VSYNC_END,    va2000_selected_mode->vmax);
    VA2000_WRITEREG(base, VA2000_REG_PIX_CLK,      va2000_selected_mode->clk);
    VA2000_WRITEREG(base, VA2000_REG_COLORMODE,    VA2000_COLORMODE_16BIT);
    VA2000_WRITEREG(base, VA2000_REG_PITCH,        va2000_selected_mode->w);
    VA2000_WRITEREG(base, VA2000_REG_PITCH_SHF,    9);
    VA2000_WRITEREG(base, VA2000_REG_WIDTH,        va2000_selected_mode->w);
    VA2000_WRITEREG(base, VA2000_REG_HEIGHT,       va2000_selected_mode->h);
    VA2000_WRITEREG(base, VA2000_REG_SCALEMODE,    0);
    VA2000_WRITEREG(base, VA2000_REG_SAFE_X2,      0x1e0);
    VA2000_WRITEREG(base, VA2000_REG_RAM_FETCH,    0x17);
    VA2000_WRITEREG(base, VA2000_REG_FETCH_PREROLL,0x1e0);
    VA2000_WRITEREG(base, VA2000_REG_PAN_HI,       VA2000_PAN_RTG_HI);
    VA2000_WRITEREG(base, VA2000_REG_PAN_LO,       VA2000_PAN_RTG_LO);
    VA2000_WRITEREG(base, VA2000_REG_CAPTURE,      VA2000_CAPTURE_RTG);
    /* Second modeline write clears display glitches (from va2000_blit.c) */
    VA2000_WRITEREG(base, VA2000_REG_HTOTAL,       va2000_selected_mode->hss);
    VA2000_WRITEREG(base, VA2000_REG_HSYNC_START,  va2000_selected_mode->hse);
    VA2000_WRITEREG(base, VA2000_REG_HSYNC_END,    va2000_selected_mode->hmax);
    VA2000_WRITEREG(base, VA2000_REG_VTOTAL,       va2000_selected_mode->vss);
    VA2000_WRITEREG(base, VA2000_REG_VSYNC_START,  va2000_selected_mode->vse);
    VA2000_WRITEREG(base, VA2000_REG_VSYNC_END,    va2000_selected_mode->vmax);

    pVA->fd      = fd;
    pRTG->devFd  = fd;          /* the layer's monitor switch uses a dup of this */
    pVA->mapSize = mapSize;
    pVA->regBase = (pointer) base;
    pVA->fbBase  = (unsigned short *)(base + VA2000_FB_OFFSET);

    /* Clear framebuffer to black via CPU — safe, known-working. */
    memset(pVA->fbBase, 0, va2000_selected_mode->h * va2000_selected_mode->w * 2);

    pRTG->frameBase   = (pointer) base;
    pRTG->fbBase      = pVA->fbBase;
    pRTG->cardPrivate = (pointer) pVA;

    ErrorF("va2000InitHW: done OK\n");
    return TRUE;
}

/*
** va2000CloseHW — restore passthrough, unmap and close.
**
** Writes VA2000_CAPTURE_PASSTHRU before releasing the mapping so that
** the native Amiga video is restored even if the caller crashes
** immediately after.
*/
void
va2000CloseHW(pRTG)
rtgScreenPtr pRTG;
{
    va2000ScreenPtr pVA = GetVA2000Screen(pRTG);

    if (va2000_blit_timeouts || va2000_blit_slow)
        ErrorF("va2000: blitter %d timeouts, %d slow waits, worst %d polls\n",
               (int) va2000_blit_timeouts, (int) va2000_blit_slow,
               (int) va2000_blit_worst);

    if (va2000_putimage_fast || va2000_putimage_slow)
        ErrorF("va2000: PutImage %d native, %d via mi\n",
               (int) va2000_putimage_fast, (int) va2000_putimage_slow);

    if (va2000_glyph_fast || va2000_glyph_slow)
        ErrorF("va2000: glyph runs %d native, %d via mi\n",
               (int) va2000_glyph_fast, (int) va2000_glyph_slow);

    if (va2000_copy_fast || va2000_copy_slow)
        ErrorF("va2000: pixmap CopyArea %d native, %d via mi\n",
               (int) va2000_copy_fast, (int) va2000_copy_slow);

    /*
    ** How much work was actually done, and by which half of the card.
    ** "blitter" is what the hardware moved; the rest the CPU wrote a word
    ** or two at a time.  The ratio is the case for or against pushing more
    ** through the blitter.
    */
    if (va2000_blits || va2000_fill_hw_px || va2000_fill_cpu_px ||
        va2000_copy_cpu_px)
    {
        ErrorF("va2000: fill %d hw + %d cpu px, copy %d hw + %d cpu px, image %d px, glyph %d px\n",
               (int) va2000_fill_hw_px, (int) va2000_fill_cpu_px,
               (int) va2000_copy_hw_px, (int) va2000_copy_cpu_px,
               (int) va2000_image_px, (int) va2000_glyph_px);

        /*
        ** Polls per blit is the -blitmin question in one number: it is what
        ** the CPU spends waiting for the hardware, per operation handed to
        ** it, and it does not depend on how big the operation was.  Set
        ** -blitmin above the size where that wait costs more than doing the
        ** fill directly.
        */
        if (va2000_paint_calls)
            ErrorF("va2000: PaintWindow %d calls, %d ms total, %d us each\n",
                   (int) va2000_paint_calls, (int)(va2000_paint_us / 1000L),
                   (int)(va2000_paint_us / va2000_paint_calls));

        if (va2000_tile_px)
            ErrorF("va2000: tiled %d px, %d wide (%d cached, %d built), %d short-loop\n",
                   (int) va2000_tile_px, (int) va2000_tile_wide,
                   (int) va2000_tile_hit, (int) va2000_tile_miss,
                   (int) va2000_tile_narrow);

        if (va2000_blits)
            ErrorF("va2000: %d blits, %d polls waiting, %d polls per blit, blitmin %d px\n",
                   (int) va2000_blits, (int) va2000_blit_polls,
                   (int)(va2000_blit_polls / va2000_blits),
                   (int) va2000_blit_min);
    }

    /*
    ** Pixmap copy sizes.  Off-screen VRAM pixmaps would let these go through
    ** the blitter, but only the large ones would gain: the blitter costs ten
    ** register writes across Zorro before it moves a pixel, which is why
    ** -blitmin exists.  If this histogram is all in the small buckets, that
    ** feature is not worth building.
    */
    {
        int i;
        long total = 0;

        for (i = 0; i < VA2000_SZBUCKETS; i++)
            total += va2000_copy_hist[i];

        if (total)
        {
            ErrorF("va2000: pixmap copy sizes (px): <64:%d 64:%d 256:%d 1K:%d 4K:%d 16K:%d 64K:%d 256K+:%d\n",
                   (int) va2000_copy_hist[0], (int) va2000_copy_hist[1],
                   (int) va2000_copy_hist[2], (int) va2000_copy_hist[3],
                   (int) va2000_copy_hist[4], (int) va2000_copy_hist[5],
                   (int) va2000_copy_hist[6], (int) va2000_copy_hist[7]);
            ErrorF("va2000: none of these used the blitter; it moves only window-to-window\n");
        }
    }

    if (!pVA)
        return;

    /* Restore passthrough — full sequence from va2000_test.c */
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_PAN_HI,       VA2000_PAN_PASSTHRU_HI);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_PAN_LO,       VA2000_PAN_PASSTHRU_LO);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_HTOTAL,       VA2000_ML_HTOTAL);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_HSYNC_START,  VA2000_ML_HSYNC_START);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_HSYNC_END,    VA2000_ML_HSYNC_END);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_VTOTAL,       VA2000_ML_VTOTAL);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_VSYNC_START,  VA2000_ML_VSYNC_START);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_VSYNC_END,    VA2000_ML_VSYNC_END);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_PIX_CLK,      1);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_COLORMODE,    VA2000_COLORMODE_16BIT);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_PITCH,        VA2000_PASSTHRU_PITCH);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_PITCH_SHF,    9);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_WIDTH,        VA2000_PASSTHRU_WIDTH);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_HEIGHT,       VA2000_PASSTHRU_HEIGHT);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_SCALEMODE,    0);
    VA2000_WRITEREG(pVA->regBase, VA2000_REG_CAPTURE,      VA2000_CAPTURE_PASSTHRU);

    (void) munmap(pVA->regBase, (size_t) pVA->mapSize);
    (void) close(pVA->fd);

    xfree((pointer) pVA);
    pRTG->cardPrivate = (pointer) NULL;
    pRTG->frameBase   = (pointer) NULL;
    pRTG->fbBase      = (unsigned short *) NULL;
}
