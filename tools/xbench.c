/*
** xbench.c -- a small, self-contained X11 benchmark for Xrtg on AMIX.
**
** Why not x11perf: the x11perf in the X11R5 binary package dies on this
** server with BadMatch on GetImage before running a single test -- its
** HardwareSync() reads a pixel from a fixed corner of its window, and
** chasing that down costs more than writing the thing it was needed for.
** This measures exactly the operations the optimisation phases touch, and
** nothing else.
**
** Build on the machine (its own compiler, its own libraries -- no ABI
** questions):
**
**     cc -O -o xbench xbench.c -lX11 -lsocket -lnsl
**
** Run against a server that is already up:
**
**     DISPLAY=:0 ./xbench            # all tests
**     DISPLAY=:0 ./xbench -n 3       # three passes, to see the spread
**
** Output is one line per test: name, operations per second, and the number
** of operations timed.  Compare two runs by eye or with diff; the numbers
** that matter are ratios between builds, not absolutes.
**
** Each test batches many operations between XSync() calls so that the
** measurement is of the server, not of the round trip.  The batch is sized
** so a slow machine still finishes in a second or two.
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define W 512
#define H 384

static Display *dpy;
static Window   win;
static GC       gc;
static int      screen;

/* ------------------------------------------------------------------ */

static double
now()
{
    struct timeval tv;
    gettimeofday(&tv, (struct timezone *) 0);
    return (double) tv.tv_sec + (double) tv.tv_usec / 1000000.0;
}

/*
** MIN_SECS -- the floor on how long a timed run may be.
**
** This machine's timings vary by a factor of three between runs of an
** unchanged binary (the saku26 demo saw 14, 15, 40, 46 and 47 ms/frame for
** one unchanged configuration).  A test that finishes in 0.15 s therefore
** measures the weather, not the server: an early version of this file read
** a 20% difference between two builds off exactly such a run, and the same
** binary later produced numbers three times apart.  So every test repeats,
** doubling its rep count, until it has run this long.
*/
#define MIN_SECS 3.0

static void
report(name, ops, secs)
char  *name;
long   ops;
double secs;
{
    if (secs <= 0.0)
        secs = 0.000001;
    printf("%-16s %10.1f ops/sec   (%ld ops in %.2f s)\n",
           name, (double) ops / secs, ops, secs);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Tests.  Each returns the number of operations it performed and fills
** *secs with the wall time the server took, XSync included. */

/*
** Fill only.  The colour is set once, outside the timed loop, on purpose:
** an XSetForeground per rectangle makes the server revalidate the GC every
** time, and then the test measures ValidateGC and the region code rather
** than filling.  The first version of this file did that, and the result
** was a 20% difference between two builds that turned out to live entirely
** in GC validation -- which is worth measuring, but as its own test.
*/
static long
t_fillrect(size, reps, secs)
int     size;
long    reps;
double *secs;
{
    double t0;
    long   i;

    XSetForeground(dpy, gc, (unsigned long) 0xf81f);
    XSync(dpy, False);
    t0 = now();
    for (i = 0; i < reps; i++)
        XFillRectangle(dpy, win, gc,
                       (int)((i * 7) % (W - size)),
                       (int)((i * 13) % (H - size)),
                       (unsigned) size, (unsigned) size);
    XSync(dpy, False);
    *secs = now() - t0;
    return reps;
}

/*
** The dispatch floor: requests that ask the server to do as close to
** nothing as the protocol allows.
**
** XNoOp is one NoOperation request: the server reads it, dispatches it and
** does nothing.  Whatever that costs is what every other request pays
** before a pixel is touched, so it bounds how much the drawing paths can
** still be worth.  If text is 42 us per character and this is 35, the inner
** loops are finished and the rest is protocol; if this is 5, they are not.
**
** It has to be XNoOp and not XSetForeground, which is what the first
** version used: Xlib caches GC changes and sends ChangeGC only when the GC
** is next used, so that test measured a client-side function call at 1.0 us
** and sent the server almost nothing.
**
** Batched between XSyncs so the round trip is not what is measured.
*/
static long
t_noop(reps, secs)
long    reps;
double *secs;
{
    double t0;
    long   i;

    XSync(dpy, False);
    t0 = now();
    for (i = 0; i < reps; i++)
        XNoOp(dpy);
    XSync(dpy, False);
    *secs = now() - t0;
    return reps;
}

/*
** GC validation: change the foreground, then draw the smallest thing that
** forces the change to take effect.  What this costs is ValidateGC plus the
** region bookkeeping around it.
*/
static long
t_gcchange(reps, secs)
long    reps;
double *secs;
{
    double t0;
    long   i;

    XSync(dpy, False);
    t0 = now();
    for (i = 0; i < reps; i++)
    {
        XSetForeground(dpy, gc, (unsigned long)(i & 0xffff));
        XFillRectangle(dpy, win, gc, (int)(i % 100), (int)(i % 100), 1, 1);
    }
    XSync(dpy, False);
    *secs = now() - t0;
    return reps;
}

static long
t_text(font, reps, secs)
XFontStruct *font;
long         reps;
double      *secs;
{
    static char *s = "The quick brown fox jumps over the lazy dog 0123456789";
    int    len = strlen(s);
    double t0;
    long   i;

    XSync(dpy, False);
    t0 = now();
    for (i = 0; i < reps; i++)
        XDrawString(dpy, win, gc, 4, (int)(12 + (i % 20) * 15), s, len);
    XSync(dpy, False);
    *secs = now() - t0;
    return reps * (long) len;      /* characters, not calls */
}

static long
t_imagetext(font, reps, secs)
XFontStruct *font;
long         reps;
double      *secs;
{
    static char *s = "The quick brown fox jumps over the lazy dog 0123456789";
    int    len = strlen(s);
    double t0;
    long   i;

    XSync(dpy, False);
    t0 = now();
    for (i = 0; i < reps; i++)
        XDrawImageString(dpy, win, gc, 4, (int)(12 + (i % 20) * 15), s, len);
    XSync(dpy, False);
    *secs = now() - t0;
    return reps * (long) len;
}

static long
t_copywin(size, reps, secs)
int     size;
long    reps;
double *secs;
{
    double t0;
    long   i;

    XSync(dpy, False);
    t0 = now();
    for (i = 0; i < reps; i++)
        XCopyArea(dpy, win, win, gc,
                  (int)((i * 3) % (W - size)), (int)((i * 5) % (H - size)),
                  (unsigned) size, (unsigned) size,
                  (int)((i * 11) % (W - size)), (int)((i * 17) % (H - size)));
    XSync(dpy, False);
    *secs = now() - t0;
    return reps;
}

static long
t_copypixwin(pix, size, reps, secs)
Pixmap  pix;
int     size;
long    reps;
double *secs;
{
    double t0;
    long   i;

    XSync(dpy, False);
    t0 = now();
    for (i = 0; i < reps; i++)
        XCopyArea(dpy, pix, win, gc, 0, 0,
                  (unsigned) size, (unsigned) size,
                  (int)((i * 11) % (W - size)), (int)((i * 17) % (H - size)));
    XSync(dpy, False);
    *secs = now() - t0;
    return reps;
}

static long
t_copywinpix(pix, size, reps, secs)
Pixmap  pix;
int     size;
long    reps;
double *secs;
{
    double t0;
    long   i;

    XSync(dpy, False);
    t0 = now();
    for (i = 0; i < reps; i++)
        XCopyArea(dpy, win, pix, gc,
                  (int)((i * 3) % (W - size)), (int)((i * 5) % (H - size)),
                  (unsigned) size, (unsigned) size, 0, 0);
    XSync(dpy, False);
    *secs = now() - t0;
    return reps;
}

static long
t_putimage(img, size, reps, secs)
XImage *img;
int     size;
long    reps;
double *secs;
{
    double t0;
    long   i;

    XSync(dpy, False);
    t0 = now();
    for (i = 0; i < reps; i++)
        XPutImage(dpy, win, gc, img, 0, 0,
                  (int)((i * 11) % (W - size)), (int)((i * 17) % (H - size)),
                  (unsigned) size, (unsigned) size);
    XSync(dpy, False);
    *secs = now() - t0;
    return reps;
}

static long
t_line(reps, secs)
long    reps;
double *secs;
{
    double t0;
    long   i;

    XSync(dpy, False);
    t0 = now();
    for (i = 0; i < reps; i++)
        XDrawLine(dpy, win, gc,
                  (int)((i * 7) % W),  (int)((i * 13) % H),
                  (int)((i * 23) % W), (int)((i * 31) % H));
    XSync(dpy, False);
    *secs = now() - t0;
    return reps;
}

/* ------------------------------------------------------------------ */

static char *only = (char *) 0;   /* -only: run just this one test */

int
main(argc, argv)
int    argc;
char **argv;
{
    XSetWindowAttributes swa;
    XFontStruct         *font;
    XImage              *img;
    XImage              *imgs;
    Pixmap               pix;
    char                *data;
    char                *small;
    double               secs;
    long                 ops;
    int                  passes = 1;
    int                  pass, i;
    int                  depth;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            passes = atoi(argv[++i]);
        else if (strcmp(argv[i], "-only") == 0 && i + 1 < argc)
            only = argv[++i];
        else
        {
            fprintf(stderr, "usage: %s [-n passes] [-only test]\n", argv[0]);
            exit(2);
        }
    }

    dpy = XOpenDisplay((char *) 0);
    if (!dpy)
    {
        fprintf(stderr, "xbench: cannot open display\n");
        exit(1);
    }
    screen = DefaultScreen(dpy);
    depth  = DefaultDepth(dpy, screen);

    /* override_redirect so no window manager is needed and the window is
    ** exactly where we put it. */
    swa.override_redirect = True;
    swa.background_pixel  = BlackPixel(dpy, screen);
    swa.backing_store     = NotUseful;
    win = XCreateWindow(dpy, RootWindow(dpy, screen), 2, 2, W, H, 0,
                        (int) CopyFromParent, InputOutput,
                        (Visual *) CopyFromParent,
                        CWOverrideRedirect | CWBackPixel | CWBackingStore,
                        &swa);
    XMapWindow(dpy, win);
    XSync(dpy, False);

    gc = XCreateGC(dpy, win, 0L, (XGCValues *) 0);
    XSetForeground(dpy, gc, WhitePixel(dpy, screen));
    XSetBackground(dpy, gc, BlackPixel(dpy, screen));

    font = XLoadQueryFont(dpy, "fixed");
    if (font)
        XSetFont(dpy, gc, font->fid);

    pix = XCreatePixmap(dpy, win, 100, 100, (unsigned) depth);
    XFillRectangle(dpy, pix, gc, 0, 0, 100, 100);

    /* An odd width on purpose: this is the case the padded-scanline bug in
    ** SetSpans got wrong, so the benchmark exercises it every run. */
    data = (char *) malloc(101 * 100 * 4);
    if (!data)
    {
        fprintf(stderr, "xbench: out of memory\n");
        exit(1);
    }
    memset(data, 0x5a, 101 * 100 * 4);
    img = XCreateImage(dpy, DefaultVisual(dpy, screen), (unsigned) depth,
                       ZPixmap, 0, data, 101, 100, 32, 0);
    if (!img)
    {
        fprintf(stderr, "xbench: XCreateImage failed\n");
        exit(1);
    }

    /*
    ** A small image as well as a large one.  101x100 is 20 KB of request
    ** body per call; 11x10 is 220 bytes.  If the two differ by roughly the
    ** byte ratio, the cost is per byte -- the transport or the write.  If
    ** they differ much less, it is per request, and no amount of work on
    ** the drawing path will move it.
    */
    small = (char *) malloc(11 * 10 * 4);
    if (!small)
    {
        fprintf(stderr, "xbench: out of memory\n");
        exit(1);
    }
    memset(small, 0x3c, 11 * 10 * 4);
    imgs = XCreateImage(dpy, DefaultVisual(dpy, screen), (unsigned) depth,
                        ZPixmap, 0, small, 11, 10, 32, 0);
    if (!imgs)
    {
        fprintf(stderr, "xbench: XCreateImage (small) failed\n");
        exit(1);
    }

    printf("xbench: %dx%d, depth %d, display %s\n",
           DisplayWidth(dpy, screen), DisplayHeight(dpy, screen),
           depth, DisplayString(dpy));
    fflush(stdout);

    for (pass = 0; pass < passes; pass++)
    {
        if (passes > 1)
        {
            printf("--- pass %d ---\n", pass + 1);
            fflush(stdout);
        }

/*
** Run one test unless -only names a different one.
**
** Isolation matters more than it looks here.  The tests share a server and
** run in sequence, so a test can be measured against a card the previous
** test left busy -- which is how a difference showed up in copywinwin100
** between two runs that differed only in a flag affecting fills.  Being able
** to run one test alone is what separates "this changed" from "this changed
** because of what ran before it".
*/
#define SCALE(call, reps0, name)                                        \
        if (!only || strcmp(only, name) == 0)                           \
        {                                                               \
            long r_ = (reps0);                                          \
            for (;;)                                                    \
            {                                                           \
                ops = call;                                             \
                if (secs >= MIN_SECS || r_ > 4000000L) break;           \
                r_ = (secs < 0.05) ? r_ * 8 : (long)(r_ * (MIN_SECS * 1.2 / secs)); \
            }                                                           \
            report(name, ops, secs);                                    \
        }

        SCALE(t_gcchange(r_, &secs),            3000L, "gcchange")
        SCALE(t_fillrect(10, r_, &secs),        4000L, "fillrect10")
        SCALE(t_fillrect(100, r_, &secs),        800L, "fillrect100")
        SCALE(t_fillrect(300, r_, &secs),        200L, "fillrect300")
        SCALE(t_line(r_, &secs),                3000L, "line")
        SCALE(t_text(font, r_, &secs),           300L, "text(chars)")
        SCALE(t_imagetext(font, r_, &secs),      300L, "imagetext(ch)")
        SCALE(t_copywin(100, r_, &secs),         600L, "copywinwin100")
        SCALE(t_copypixwin(pix, 100, r_, &secs), 200L, "copypixwin100")
        SCALE(t_copywinpix(pix, 100, r_, &secs), 200L, "copywinpix100")
        SCALE(t_putimage(img, 100, r_, &secs),   150L, "putimage101x100")
        SCALE(t_putimage(imgs, 10, r_, &secs),  2000L, "putimage11x10")

        /*
        ** Last, and not scaled, because it breaks the connection.
        **
        ** A burst of small requests -- 20000 four-byte NoOperations -- makes
        ** this server drop the client a few thousand requests later:
        ** "XIO: fatal IO error 32 (Broken pipe) after 23091 requests".  It
        ** reproduces, and it is worth someone's time on its own; it may or
        ** may not be related to the desktop stalls being chased.  Running it
        ** last means the measurement survives even though the client does
        ** not, and 20000 is enough to read a rate off.
        */
        if (!only || strcmp(only, "noop") == 0)
        {
            ops = t_noop(20000L, &secs);      report("noop", ops, secs);
        }
    }

    XFreePixmap(dpy, pix);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
