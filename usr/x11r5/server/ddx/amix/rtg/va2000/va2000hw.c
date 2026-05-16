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
** The register sequence written by va2000InitHW() is the exact sequence
** verified working in blit_test3:
**
**   REG(0x70)=840   HTOTAL
**   REG(0x72)=968   HSYNC_START
**   REG(0x74)=1056  HSYNC_END
**   REG(0x76)=601   VTOTAL
**   REG(0x78)=605   VSYNC_START
**   REG(0x7a)=628   VSYNC_END
**   REG(0x7c)=1
**   REG(0x0e)=1
**   REG(0x58)=800   PITCH (in pixels)
**   REG(0x06)=800   WIDTH
**   REG(0x08)=600   HEIGHT
**   REG(0x4e)=0     CAPTURE = RTG active
*/

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include "../../amix.h"
#include "../rtg.h"
#include "va2000.h"

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

    /*
    ** Write mode registers — order matches blit_test3 working sequence.
    ** Horizontal timing first, then vertical, then geometry, then capture.
    */
    VA2000_WRITEREG(base, VA2000_REG_HTOTAL,      VA2000_ML_HTOTAL);
    VA2000_WRITEREG(base, VA2000_REG_HSYNC_START,  VA2000_ML_HSYNC_START);
    VA2000_WRITEREG(base, VA2000_REG_HSYNC_END,    VA2000_ML_HSYNC_END);
    VA2000_WRITEREG(base, VA2000_REG_VTOTAL,       VA2000_ML_VTOTAL);
    VA2000_WRITEREG(base, VA2000_REG_VSYNC_START,  VA2000_ML_VSYNC_START);
    VA2000_WRITEREG(base, VA2000_REG_VSYNC_END,    VA2000_ML_VSYNC_END);
    VA2000_WRITEREG(base, VA2000_REG_UNKNOWN_7C,   1);
    VA2000_WRITEREG(base, VA2000_REG_UNKNOWN_0E,   1);
    VA2000_WRITEREG(base, VA2000_REG_PITCH,        VA2000_WIDTH);
    VA2000_WRITEREG(base, VA2000_REG_WIDTH,        VA2000_WIDTH);
    VA2000_WRITEREG(base, VA2000_REG_HEIGHT,       VA2000_HEIGHT);
    VA2000_WRITEREG(base, VA2000_REG_CAPTURE,      VA2000_CAPTURE_RTG);

    pVA->fd      = fd;
    pVA->regBase = (pointer) base;
    pVA->fbBase  = (unsigned short *)(base + VA2000_FB_OFFSET);

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

    VA2000_WRITEREG(pVA->regBase, VA2000_REG_CAPTURE, VA2000_CAPTURE_PASSTHRU);

    (void) munmap(pVA->regBase, VA2000_MMAP_SIZE);
    (void) close(pVA->fd);

    xfree((pointer) pVA);
    pRTG->cardPrivate = (pointer) NULL;
    pRTG->frameBase   = (pointer) NULL;
    pRTG->fbBase      = (unsigned short *) NULL;
}
