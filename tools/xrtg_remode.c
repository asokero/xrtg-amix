/*
** xrtg_remode.c -- put the VA2000 back into Xrtg's mode without restarting it.
**
** THE PROBLEM THIS SOLVES.  Xrtg's picture does not come from the AMIX screen
** system.  rtgOpenInput() takes the input with SIOCACTIVATE and deliberately
** never calls DisplayScreen -- doing so would seize the ECS display and earn
** the server a SIGHUP when the native console lost its screen.  So the X
** screen is not in the Ctrl+Alt+Fn rotation and there is no key combination
** that switches back to it: what puts Xrtg on the monitor is the VA2000 being
** in RTG mode, and the screen switcher knows nothing about that.
**
** Xrtg writes those mode registers exactly once, in va2000InitHW, and has no
** re-activation hook.  Any program that takes the board and then restores
** Amiga passthrough on its way out -- a console framebuffer game, va2000_test,
** va2000_restore itself -- therefore leaves the desktop invisible and
** unreachable, even though the server is still running and every one of its
** pixels is still sitting at framebuffer offset 0, untouched.
**
** All that is missing is the mode and the pan.  This writes them back, in
** Xrtg's own order and with Xrtg's own values, and the desktop reappears with
** its clients intact.  The alternative is restarting the server and losing the
** session.
**
** QUIT THE OTHER PROGRAM FIRST.  If something else still owns the board it
** will keep writing, and the two of you will fight over the display.  There is
** no way to detect that from here: nothing arbitrates this hardware.
**
** THE MODE HAS TO BE NAMED because it cannot be discovered.  These registers
** are write-only -- read one back and it answers 0x005a whatever was put in
** it.  Pass the same mode Xrtg was started with; the table below is a copy of
** va2000_modes[] in va2000hw.c, and if that table ever changes this one has to
** change with it.
**
** Build on the machine, with its own compiler:
**
**     cc -O -o xrtg_remode xrtg_remode.c
**
** Run as root -- the mmap of /dev/va2000 needs it:
**
**     ./xrtg_remode 1280x720
**     ./xrtg_remode                 (defaults to 800x600, as Xrtg does)
*/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

#define REGWIN          0x00010000UL    /* register window at aperture 0 */

/* AMIX's <sys/mman.h> declares mmap() and nothing else -- there is no munmap
** in it.  Declared K&R style, exactly as the header declares mmap, so there is
** no prototype here to disagree with the library symbol. */
extern int munmap();

/* Offsets from va2000.h.  The names there are one step out of phase with what
** the registers actually are -- REG_HTOTAL receives the mode's hss -- so these
** are named for what they do, and the values below are in the same order Xrtg
** writes them. */
#define R_SCALEMODE     0x04
#define R_WIDTH         0x06
#define R_HEIGHT        0x08
#define R_COLORMODE     0x0e
#define R_SAFE_X2       0x14
#define R_RAM_FETCH     0x18
#define R_FETCH_PREROLL 0x1a
#define R_PAN_HI        0x38
#define R_PAN_LO        0x3a
#define R_CAPTURE       0x4e
#define R_PITCH         0x58
#define R_PITCH_SHF     0x5c
#define R_HSS           0x70
#define R_HSE           0x72
#define R_HMAX          0x74
#define R_VSS           0x76
#define R_VSE           0x78
#define R_VMAX          0x7a
#define R_PIX_CLK       0x7c

#define COLORMODE_16BIT 1
#define CAPTURE_RTG     0

#define REG(off)        (*((volatile unsigned short *)(regs + (off))))

struct mode
{
    char *name;
    int   w, h;
    int   hss, hse, hmax;
    int   vss, vse, vmax;
    int   clk;
};

/* A copy of va2000_modes[] in
** usr/x11r5/server/ddx/amix/rtg/va2000/va2000hw.c. */
static struct mode modes[] =
{
    { "640x480",   640,  480,  656,  752,  800,  490,  492,  525, 1 },
    { "800x600",   800,  600,  840,  968, 1056,  601,  605,  628, 1 },
    { "1024x768", 1024,  768, 1048, 1184, 1328,  771,  777,  806, 0 },
    { "1280x720", 1280,  720, 1390, 1430, 1650,  725,  730,  750, 0 },
    { "1280x1024",1280, 1024, 1328, 1440, 1600, 1025, 1028, 1066, 3 },
    { "1920x1080",1920, 1080, 1992, 2000, 2287, 1083, 1088, 1109, 0 },
    { (char *)0,     0,    0,    0,    0,    0,    0,    0,    0, 0 }
};

static char *regs = (char *)-1;

static void
usage(argv0)
char *argv0;
{
    struct mode *m;

    fprintf(stderr, "usage: %s [mode]\n", argv0);
    fprintf(stderr, "modes:");
    for (m = modes; m->name; m++)
        fprintf(stderr, " %s", m->name);
    fprintf(stderr, "\ndefault: 800x600, which is also Xrtg's default\n");
}

int
main(argc, argv)
int argc;
char **argv;
{
    struct mode   *m;
    char          *want;
    int            fd;
    unsigned short pan_hi, pan_lo;

    setbuf(stdout, (char *)0);

    want = argc > 1 ? argv[1] : "800x600";
    if (!strcmp(want, "-h") || !strcmp(want, "-help"))
    {
        usage(argv[0]);
        return 2;
    }

    for (m = modes; m->name; m++)
        if (!strcmp(m->name, want))
            break;
    if (!m->name)
    {
        fprintf(stderr, "%s: unknown mode \"%s\"\n", argv[0], want);
        usage(argv[0]);
        return 2;
    }

    fd = open("/dev/va2000", O_RDWR);
    if (fd < 0)
    {
        perror("open /dev/va2000");
        fprintf(stderr, "(this needs root, and the driver installed)\n");
        return 1;
    }

    regs = (char *)mmap((caddr_t)0, (size_t)REGWIN,
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0);
    if (regs == (char *)-1)
    {
        perror("mmap /dev/va2000");
        close(fd);
        return 1;
    }

    /* The pan registers are the one part of this that reads back, so say what
    ** was there.  0xf8 in the high half is the Amiga passthrough buffer, which
    ** is the state a console program leaves behind and the usual reason for
    ** running this at all. */
    pan_hi = REG(R_PAN_HI);
    pan_lo = REG(R_PAN_LO);
    printf("pan on entry: 0x%04x%04x%s\n", pan_hi, pan_lo,
           pan_hi == 0xf8 ? "  (Amiga passthrough)" : "");

    if (pan_hi == 0 && pan_lo == 0)
        printf("NOTE: already pointing at framebuffer offset 0, where Xrtg's\n"
               "      screen lives.  If the display is wrong anyway it is the\n"
               "      mode that is wrong, and writing it again is the fix.\n");

    printf("writing %s (%dx%d), pan 0, RTG on\n", m->name, m->w, m->h);

    /* Xrtg's own sequence, from va2000InitHW. */
    REG(R_HSS)           = m->hss;
    REG(R_HSE)           = m->hse;
    REG(R_HMAX)          = m->hmax;
    REG(R_VSS)           = m->vss;
    REG(R_VSE)           = m->vse;
    REG(R_VMAX)          = m->vmax;
    REG(R_PIX_CLK)       = m->clk;
    REG(R_COLORMODE)     = COLORMODE_16BIT;
    REG(R_PITCH)         = m->w;        /* 16bpp: pitch in pixels == width */
    REG(R_PITCH_SHF)     = 9;
    REG(R_WIDTH)         = m->w;
    REG(R_HEIGHT)        = m->h;
    REG(R_SCALEMODE)     = 0;
    REG(R_SAFE_X2)       = 0x1e0;
    REG(R_RAM_FETCH)     = 0x17;
    REG(R_FETCH_PREROLL) = 0x1e0;
    REG(R_PAN_HI)        = 0;
    REG(R_PAN_LO)        = 0;
    REG(R_CAPTURE)       = CAPTURE_RTG;

    /* Written twice on purpose: one pass leaves display glitches.  Xrtg does
    ** the same, and so does va2000_blit.c where it came from. */
    REG(R_HSS)  = m->hss;
    REG(R_HSE)  = m->hse;
    REG(R_HMAX) = m->hmax;
    REG(R_VSS)  = m->vss;
    REG(R_VSE)  = m->vse;
    REG(R_VMAX) = m->vmax;

    munmap((caddr_t)regs, (size_t)REGWIN);
    regs = (char *)-1;
    close(fd);

    printf("done.  Xrtg's pixels were never touched, so the desktop should be\n");
    printf("back as it was, with its clients still running.\n");
    return 0;
}
