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

    base = (caddr_t) mmap(0, VA2000_MMAP_SIZE,
                          PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ErrorF("va2000InitHW: base=%p\n", base);
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

    (void) munmap(pVA->regBase, VA2000_MMAP_SIZE);
    (void) close(pVA->fd);

    xfree((pointer) pVA);
    pRTG->cardPrivate = (pointer) NULL;
    pRTG->frameBase   = (pointer) NULL;
    pRTG->fbBase      = (unsigned short *) NULL;
}
