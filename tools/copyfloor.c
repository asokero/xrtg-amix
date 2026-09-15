/* copyfloor.c -- the number neither project had: what a host-to-VRAM COPY
 * costs, as against the STORE bandwidth va2000_bench measures.
 *
 * va2000_bench times fill32(), which writes a constant.  PutImage and the
 * tiled root repaint both read a pixel from host memory and write it to the
 * board, so the 68060 does a load and a store per longword and cannot keep
 * the write buffer fed the way a fill can.  The copy floor is therefore
 * above the store floor by an amount nobody had measured, and every
 * viability estimate on both sides of this work has been using the store
 * figure in its place.
 *
 * Writes only into the framebuffer area, never a register, so it is safe to
 * run with the board in passthrough and no X server up.
 */
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>

#define DEV      "/dev/va2000"
#define FB_OFF   0x10000
#define MAPSZ    0x200000
#define BYTES    (1280L * 720L * 2L)
#define MINSECS  2.0

static double now()
{
    struct timeval t;
    gettimeofday(&t, (struct timezone *) 0);
    return (double) t.tv_sec + (double) t.tv_usec / 1000000.0;
}

/*
** Two shapes of each, and the difference between them is the point.
**
** The plain loop measures the bus plus its own per-iteration overhead, and
** on a machine with the 68060 branch cache off that overhead is not small.
** Measuring only that shape makes the probe sensitive to a CACR bit other
** than the one under test, which would let a branch-cache change look like a
** store-buffer result.
**
** The unrolled shape is also what the driver actually does: va2000FillRun and
** va2000CopyRun both write four longwords per iteration.  So the x4 rows are
** the ones representing this server, and the plain rows are there to show how
** much of any change was loop and how much was bus.
*/
static void fill32(d, n, v)
unsigned long *d; long n; unsigned long v;
{ while (n-- > 0) *d++ = v; }

static void fill32x4(d, n, v)
unsigned long *d; long n; unsigned long v;
{
    while (n >= 4) { d[0] = v; d[1] = v; d[2] = v; d[3] = v; d += 4; n -= 4; }
    while (n-- > 0) *d++ = v;
}

static void copy32(d, s, n)
unsigned long *d; unsigned long *s; long n;
{ while (n-- > 0) *d++ = *s++; }

static void copy32x4(d, s, n)
unsigned long *d; unsigned long *s; long n;
{
    while (n >= 4) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                     d += 4; s += 4; n -= 4; }
    while (n-- > 0) *d++ = *s++;
}

/* mode: 0 plain fill, 1 unrolled fill, 2 plain copy, 3 unrolled copy */
static void run(label, mode, dst, src, nlongs)
char *label; int mode; unsigned long *dst; unsigned long *src; long nlongs;
{
    double t0, t1;
    long   passes = 0;
    double mb;

    t0 = now();
    do {
        switch (mode) {
        case 0: fill32(dst, nlongs, 0x0F0F0F0FUL);   break;
        case 1: fill32x4(dst, nlongs, 0x0F0F0F0FUL); break;
        case 2: copy32(dst, src, nlongs);            break;
        default: copy32x4(dst, src, nlongs);         break;
        }
        passes++;
        t1 = now();
    } while (t1 - t0 < MINSECS);

    mb = (double)(passes * nlongs * 4L) / (t1 - t0);
    printf("  %-34s %8.0f KB/s  %7.4f us/px (16bpp)\n",
           label, mb / 1024.0, (t1 - t0) * 1000000.0 / (double)(passes * nlongs * 2L));
}

main()
{
    int             fd;
    char           *base;
    unsigned long  *fb, *ram, *ram2;
    long            nlongs = BYTES / 4;

    fd = open(DEV, O_RDWR);
    if (fd < 0) { perror("open " DEV); exit(1); }
    base = (char *) mmap((caddr_t) 0, (size_t) MAPSZ,
                         PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == (char *) -1) { perror("mmap"); exit(1); }

    fb   = (unsigned long *)(base + FB_OFF);
    ram  = (unsigned long *) malloc((size_t) BYTES);
    ram2 = (unsigned long *) malloc((size_t) BYTES);
    if (!ram || !ram2) { fprintf(stderr, "malloc failed\n"); exit(1); }
    fill32(ram, nlongs, 0x12345678UL);

    printf("copyfloor: %ld KB per pass, 32-bit accesses, KB = 1024\n",
           BYTES / 1024);
    printf("  x4 rows unroll four longwords, as the driver's inner loops do.\n\n");
    run("board  fill  x4",   1, fb,   (unsigned long *) 0, nlongs);
    run("board  fill  plain",0, fb,   (unsigned long *) 0, nlongs);
    run("board  copy  x4",   3, fb,   ram,  nlongs);
    run("board  copy  plain",2, fb,   ram,  nlongs);
    run("local  fill  x4",   1, ram2, (unsigned long *) 0, nlongs);
    run("local  fill  plain",0, ram2, (unsigned long *) 0, nlongs);
    run("local  copy  x4",   3, ram2, ram,  nlongs);
    run("local  copy  plain",2, ram2, ram,  nlongs);
    return 0;
}
