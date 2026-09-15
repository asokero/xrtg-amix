#ifndef VA2000_H
#define VA2000_H
/*
**  $Filename: ddx/amix/rtg/va2000/va2000.h $
**
**  MNT VA2000 RTG card driver for xamix-rtg.
**
**  Hardware summary:
**    - Zorro III card, direct-mapped framebuffer
**    - 16-bit TrueColor, pixel format RGB565
**    - Framebuffer starts at offset VA2000_FB_OFFSET within mmap region
**    - Write VA2000_REG_CAPTURE to VA2000_CAPTURE_RTG to activate RTG mode
**    - 800x600 default: pitch = 800 pixels * 2 bytes = 1600 bytes/line
*/

#include "../rtg.h"

/* ======================================================================= */
/* = Device and memory map                                               = */
/* ======================================================================= */

#define VA2000_DEV          "/dev/va2000"
#define VA2000_MMAP_SIZE    0x200000        /* 2 MB total address space     */
#define VA2000_FB_OFFSET    0x10000         /* framebuffer within mmap      */
#define VA2000_MAP_SLACK    0x40000         /* 256 KB mapped past the mode  */

/*
** Driver ioctls we use.  Numbers must match va2000-amix/src/va2000.h.
** Both return their value through the SVR4 ioctl return value (*rvalp),
** so the call itself yields the answer:
**
**     size = ioctl(fd, SVGAIOCGetFBufSize, 0);
**
** An older driver that does not implement them returns -1 / EINVAL, which
** is why every caller must have a fallback.
*/
#define VA2000_SVGAIOC          0xe300
#define VA2000_VA2IOC           ('V' << 8)
#ifndef SVGAIOCGetFBufSize
#define SVGAIOCGetFBufSize      (VA2000_SVGAIOC | 0x04)
#endif
#ifndef VA2IOC_GETFW
#define VA2IOC_GETFW            (VA2000_VA2IOC | 1)
#endif

/* ======================================================================= */
/* = Video mode table                                                    = */
/* ======================================================================= */

typedef struct _VA2000ModeRec {
    char *name;     /* "800x600" etc.                                       */
    int   w;        /* width in pixels                                      */
    int   h;        /* height in pixels                                     */
    int   hss;      /* H sync start (HTOTAL register)                       */
    int   hse;      /* H sync end   (HSYNC_START register)                  */
    int   hmax;     /* H max        (HSYNC_END register)                    */
    int   vss;      /* V sync start (VTOTAL register)                       */
    int   vse;      /* V sync end   (VSYNC_START register)                  */
    int   vmax;     /* V max        (VSYNC_END register)                    */
    int   clk;      /* pixel clock: 0=75MHz, 1=40MHz, 3=100MHz             */
} VA2000ModeRec, *VA2000ModePtr;

extern VA2000ModePtr va2000_selected_mode; /* set by -mode WxH, default 800x600 */

/* ======================================================================= */
/* = Board capabilities, probed once at InitHW                           = */
/* ======================================================================= */

/*
** The same binary has to serve two very different machines: a stock
** A3000UX (68030, VA2000 in Zorro II firmware, 4 MB aperture) and a
** 68040/060 kernel with the card in Zorro III (32 MB aperture).  Rather
** than build two servers, probe the board once and derive the defaults
** from what is actually there.
**
** The aperture size is the discriminator: AutoConfig reports 4 MB for a
** Zorro II board and 32 MB for a Zorro III one (va2000-amix/src/va2000.c).
** -bus z2|z3 overrides the guess.
*/

#define VA2000_BUS_UNKNOWN  0
#define VA2000_BUS_Z2       1
#define VA2000_BUS_Z3       2

/* Aperture at or above this size means the board came up in Zorro III. */
#define VA2000_Z3_MIN_SIZE  0x800000L       /* 8 MB                         */

typedef struct _VA2000CapsRec {
    long fbSize;        /* framebuffer bytes reported by the driver, or 0    */
    int  bus;           /* VA2000_BUS_*                                      */
    int  busForced;     /* TRUE when -bus set it rather than the probe       */
    int  fwVersion;     /* firmware version, or 0 if the ioctl failed        */
} VA2000CapsRec;

extern VA2000CapsRec va2000_caps;

/* ======================================================================= */
/* = Runtime feature switches                                            = */
/* ======================================================================= */

/*
** Every optimisation added on this branch gets a bit here, so a bug report
** from a machine we cannot test can be narrowed down with one flag:
**
**     Xrtg :0 -compat        every fast path off, original behaviour
**
** Bits are checked at the point of use, never at compile time, so both
** target machines run the same binary and the same code paths are
** reachable on either one.
*/

#define VA2000_OPT_GLYPH     0x0001     /* native 16-bit glyph blitter      */
#define VA2000_OPT_COPY      0x0002     /* direct window<->pixmap CopyArea  */
#define VA2000_OPT_PUTIMAGE  0x0004     /* native ZPixmap PutImage          */
#define VA2000_OPT_LONGWORD  0x0008     /* 32-bit stores into VRAM          */
#define VA2000_OPT_ASYNCBLT  0x0010     /* do not wait for blitter to finish */
#define VA2000_OPT_TILE      0x0020     /* real tile / stipple fills        */
#define VA2000_OPT_BSTORE    0x0040     /* backing store (memory hungry)    */
#define VA2000_OPT_VRAMPIX   0x0080     /* pixmaps in off-screen VRAM       */
#define VA2000_OPT_STALLWATCH 0x0100    /* time the gaps in the select loop */

/*
** Defaults.  ASYNCBLT is off because the blitter status register is not
** fully trusted yet (see the BLITWAIT note in va2000draw.c); BSTORE is off
** because a stock A3000UX may have only 4-8 MB and one 800x600 window costs
** 960 KB.  Both are opt-in.
*/
#define VA2000_OPT_DEFAULT \
    (VA2000_OPT_GLYPH | VA2000_OPT_COPY | VA2000_OPT_PUTIMAGE | \
     VA2000_OPT_LONGWORD | VA2000_OPT_TILE | VA2000_OPT_VRAMPIX | \
     VA2000_OPT_STALLWATCH)

extern unsigned long va2000_options;    /* -vaopt / -compat / -bstore       */
extern int  va2000_blit_min;            /* -blitmin: CPU below this area    */
extern int  va2000_bus_override;        /* -bus: VA2000_BUS_* or UNKNOWN    */

#define VA2000_OPT(bit)  ((va2000_options & (bit)) != 0)

/*
** Blitter/CPU crossover, in pixels of rectangle area.
**
** A blitFill costs ten register writes and two status polls over Zorro
** whatever the rectangle's size, so below some area the CPU is ahead --
** and window borders, which PaintWindow fills, are strips one to four
** pixels wide.  The default is 0, meaning always use the blitter, because
** the crossover has not been measured on either bus yet and a guess here
** would be a guess in the default path.  -blitmin N sets it.
**
** To find it: xbench's fillrect10 / fillrect100 / fillrect300 against a
** few values of -blitmin.
*/
#define VA2000_BLIT_MIN_DEFAULT  0      /* 0 = always use the blitter       */

/*
** Bounded blitter wait.
**
** The original loop was unbounded and a stuck blitter locked the server.
** The first bound was set by counting pixels, which was the wrong unit: a
** poll is a Zorro register read, not a pixel, and the blitter finishes in
** its own time however fast we ask.  Two million polls is something like
** half a second of spinning, which is long enough to be felt as the
** desktop stopping.
**
** Sized from wall time instead.  The longest legitimate blit is a full
** screen: 1280x720 is 921600 pixels, and the blitter fills at about
** 44 Mpixel/s, so roughly 21 ms.  A poll is on the order of 200 ns, so
** 500000 polls is about 100 ms -- comfortably above any real blit and
** short enough that giving up is a hiccup rather than a stall.
**
** SLOW is the interesting number.  A wait that runs long and then
** succeeds writes nothing to the log under the old scheme, so a blitter
** that is merely slow looks exactly like one that is fine.  Waits past
** this many polls (about 10 ms) are counted and the first few logged.
*/
#define VA2000_BLIT_TIMEOUT      500000L
#define VA2000_BLIT_SLOW         50000L
#define VA2000_BLIT_TIMEOUT_LOG  8      /* stop logging after this many     */

extern long va2000_blit_slow;           /* waits longer than SLOW polls     */
extern long va2000_blit_worst;          /* the longest wait seen, in polls  */

extern long va2000_blit_timeouts;       /* count, reported at CloseScreen   */
extern long va2000_putimage_fast;       /* PutImage calls the native path took */
extern long va2000_putimage_slow;       /* PutImage calls that fell back to mi */
extern long va2000_glyph_fast;          /* glyph runs drawn natively          */
extern long va2000_glyph_slow;          /* glyph runs handed to mi            */
extern long va2000_copy_fast;           /* pixmap CopyArea done natively      */
extern long va2000_copy_slow;           /* pixmap CopyArea handed to mi       */

/*
** Work counters, in units of the work itself rather than of calls.
**
** These exist to answer one question: when the server spends seconds awake
** in one go, was it drawing or was it somewhere else entirely?  A stretch
** that shows near-zero of all of these was not spent in this driver, and
** that is worth knowing before optimising any part of it.
**
** They are plain longs bumped without locking, which is safe here: the X
** server is single threaded and these are read by the block handler on the
** same thread that wrote them.
*/
extern long va2000_blits;               /* blitter operations issued          */
extern long va2000_blit_polls;          /* BLITWAIT poll iterations           */
extern long va2000_fill_hw_px;          /* fill pixels the blitter wrote      */
extern long va2000_fill_cpu_px;         /* fill pixels the CPU wrote          */
extern long va2000_copy_hw_px;          /* copy pixels the blitter moved      */
extern long va2000_copy_cpu_px;         /* copy pixels the CPU moved          */
extern long va2000_image_px;            /* of the CPU copies, PutImage's      */
extern long va2000_glyph_px;            /* pixels written by glyph blitting   */
extern long va2000_tile_px;             /* of the CPU copies, tiled fills'    */
extern long va2000_tile_wide;           /* tile rows using the expanded path  */
extern long va2000_tile_narrow;         /* tile rows using the short loop     */
extern long va2000_tile_hit;            /* expanded rows reused from cache    */
extern long va2000_tile_miss;           /* expanded rows built                */
extern long va2000_paint_us;            /* microseconds inside PaintWindow    */
extern long va2000_paint_calls;         /* PaintWindow calls timed            */

/*
** These are disjoint on purpose.  The first version counted blitter and CPU
** work into one fill total and one copy total, which makes the -blitmin
** question -- at what size does the blitter stop being worth its setup --
** impossible to read off a session.  It also counted PutImage's pixels twice,
** once as image and once again as copy, because PutImage writes through the
** same run copier; a session reported 177 M copied of which 168 M was the
** same 168 M reported as image.
*/

/*
** Pixmap copy sizes, as a histogram of area in pixels.  Bucket n holds
** copies of 2^(2n+6) pixels and up -- 0 is under 64, then 256, 1K, 4K, 16K,
** 64K, 256K and above.  The question it answers is whether pixmap copies in
** real use are ever large enough for the blitter to be worth its setup
** cost, which is what off-screen VRAM pixmaps would depend on.
*/
#define VA2000_SZBUCKETS 8
extern long va2000_copy_hist[VA2000_SZBUCKETS];

/* Compile-time default: 800x600 — used only as fallback constants */
#define VA2000_WIDTH        800
#define VA2000_HEIGHT       600
#define VA2000_BPP          16
#define VA2000_DEPTH        16
#define VA2000_PITCH        (VA2000_WIDTH * 2)  /* 1600 bytes per scanline  */

/* ======================================================================= */
/* = Hardware registers (byte offsets from mmap base)                   = */
/* ======================================================================= */

/* Display controller registers (names from va2000_test.c) */
#define VA2000_REG_SCALEMODE     0x04   /* scale mode (0=normal)            */
#define VA2000_REG_WIDTH         0x06   /* display width in pixels          */
#define VA2000_REG_HEIGHT        0x08   /* display height in pixels         */
#define VA2000_REG_COLORMODE     0x0e   /* color mode                       */
#define VA2000_COLORMODE_16BIT   1      /* RGB565                           */
#define VA2000_REG_SAFE_X2       0x14   /* display timing (0x1e0)           */
#define VA2000_REG_RAM_FETCH     0x18   /* RAM fetch timing (0x17)          */
#define VA2000_REG_FETCH_PREROLL 0x1a   /* fetch preroll (0x1e0)            */
#define VA2000_REG_PAN_HI        0x38   /* display start address high word  */
#define VA2000_REG_PAN_LO        0x3a   /* display start address low word   */

/* Pan values: RTG points to framebuffer[0]; passthrough to Amiga video area */
#define VA2000_PAN_RTG_HI        0
#define VA2000_PAN_RTG_LO        0
#define VA2000_PAN_PASSTHRU_HI   0xf8
#define VA2000_PAN_PASSTHRU_LO   0

/* Capture / RTG mode select */
#define VA2000_REG_CAPTURE       0x4e   /* 0=RTG active, 1=passthrough      */
#define VA2000_CAPTURE_RTG       0
#define VA2000_CAPTURE_PASSTHRU  1

#define VA2000_REG_PITCH         0x58   /* framebuffer pitch in pixels      */
#define VA2000_REG_PITCH_SHF     0x5c   /* pitch shift (9)                  */

/* Modeline registers */
#define VA2000_REG_HTOTAL        0x70   /* H sync start                     */
#define VA2000_REG_HSYNC_START   0x72   /* H sync end                       */
#define VA2000_REG_HSYNC_END     0x74   /* H max                            */
#define VA2000_REG_VTOTAL        0x76   /* V sync start                     */
#define VA2000_REG_VSYNC_START   0x78   /* V sync end                       */
#define VA2000_REG_VSYNC_END     0x7a   /* V max                            */
#define VA2000_REG_PIX_CLK       0x7c   /* pixel clock                      */

/* Passthrough mode geometry (native Amiga video: 640x480) */
#define VA2000_PASSTHRU_WIDTH    640
#define VA2000_PASSTHRU_HEIGHT   480
#define VA2000_PASSTHRU_PITCH    320    /* pitch in pixels (640px / 2)      */

/* ======================================================================= */
/* = Default modeline values for 800x600                                = */
/* ======================================================================= */

#define VA2000_ML_HTOTAL        840
#define VA2000_ML_HSYNC_START   968
#define VA2000_ML_HSYNC_END     1056
#define VA2000_ML_VTOTAL        601
#define VA2000_ML_VSYNC_START   605
#define VA2000_ML_VSYNC_END     628

/* ======================================================================= */
/* = Pixel format: RGB565                                                = */
/* ======================================================================= */

#define VA2000_RED_MASK     0xF800
#define VA2000_GREEN_MASK   0x07E0
#define VA2000_BLUE_MASK    0x001F
#define VA2000_RED_SHIFT    11
#define VA2000_GREEN_SHIFT  5
#define VA2000_BLUE_SHIFT   0

/*
** Build an RGB565 pixel from three 8-bit components (0–255 each).
*/
#define VA2000_PIXEL(r, g, b) \
    ( (unsigned short)(((r) & 0xF8) << 8) | \
      (unsigned short)(((g) & 0xFC) << 3) | \
      (unsigned short)( (b)         >> 3) )

/*
** Build an RGB565 pixel from X11 16-bit color components (0–65535 each).
** X stores color values as 16-bit; shift down to the hardware field width.
*/
#define VA2000_PIXEL_X11(r, g, b) \
    ( (unsigned short)(((r) >> 8) & 0xF8) << 8 | \
      (unsigned short)(((g) >> 8) & 0xFC) << 3 | \
      (unsigned short)(((b) >> 8)        ) >> 3 )

/*
** Address of pixel (x, y) in the framebuffer.
*/
#define VA2000_PIXADDR(fbbase, x, y) \
    ((unsigned short *)(fbbase) + (y) * va2000_selected_mode->w + (x))

/*
** Write / read a register in the control area (before the framebuffer).
*/
#define VA2000_WRITEREG(base, reg, val) \
    (*((volatile unsigned short *)((char *)(base) + (reg))) = (unsigned short)(val))
#define VA2000_READREG(base, reg) \
    (*((volatile unsigned short *)((char *)(base) + (reg))))

/* ---------------------------------------------------------------------- */
/* Blitter registers (NetBSD mntvareg.h offsets)                           */

/*
** Blitter registers — offsets from mmap base (verified against va2000_blittest.c).
** NOTE: 0x1c/0x1e are the BLITTER's own pitch/colormode (separate from the
**       display pitch at 0x58/0x5c).  0x40/0x42 are the source-address pointer
**       that must be set to the VRAM word-address of the top-left source row
**       before each blit operation.
*/
#define VA2000_BLT_ROWPITCH  0x1c   /* blitter row pitch in pixels          */
#define VA2000_BLT_COLORMODE 0x1e   /* blitter color mode: 1=16bit          */
#define VA2000_BLT_X1        0x20   /* destination x1                       */
#define VA2000_BLT_Y1        0x22   /* destination y1                       */
#define VA2000_BLT_X2        0x24   /* destination x2 (inclusive)           */
#define VA2000_BLT_Y2        0x26   /* destination y2 (inclusive)           */
#define VA2000_BLT_RGB16     0x28   /* fill color (16-bit RGB565)           */
#define VA2000_BLT_ENABLE    0x2A   /* write to fire; poll until 0 = done   */
#define   VA2000_BLT_FILL    0x0001 /* fill [x1,y1]-[x2,y2] with RGB16     */
#define   VA2000_BLT_COPY    0x0002 /* copy [x3,y3]-[x4,y4]->[x1,y1]       */
#define VA2000_BLT_X3        0x2C   /* source x1 (copy)                     */
#define VA2000_BLT_Y3        0x2E   /* source y1 (copy)                     */
#define VA2000_BLT_X4        0x30   /* source x2 (copy, inclusive)          */
#define VA2000_BLT_Y4        0x32   /* source y2 (copy, inclusive)          */
#define VA2000_BLT_SRC_HI    0x40   /* blitter_base  [23:16] — dest row addr */
#define VA2000_BLT_SRC_LO    0x42   /* blitter_base  [15:0]  — dest row addr */
#define VA2000_BLT_SRC2_HI   0x44   /* blitter_base2 [23:16] — src  row addr */
#define VA2000_BLT_SRC2_LO   0x46   /* blitter_base2 [15:0]  — src  row addr */

/* Word offset of framebuffer start within the mmap region (FB_OFFSET/2) */
#define VA2000_FB_WORDS      (VA2000_FB_OFFSET / 2)

/* ======================================================================= */
/* = Card-specific screen private                                        = */
/* ======================================================================= */

typedef struct _va2000ScreenRec {
    int             fd;         /* open file descriptor for VA2000_DEV      */
    long            mapSize;    /* bytes mmap'd; munmap needs the same size */
    pointer         regBase;    /* mmap base (registers + framebuffer)       */
    unsigned short *fbBase;     /* pixel (0,0): regBase + VA2000_FB_OFFSET   */
} va2000ScreenRec, *va2000ScreenPtr;

#define GetVA2000Screen(rtgp) \
    ((va2000ScreenPtr)(rtgp)->cardPrivate)

/* ======================================================================= */
/* = Function declarations                                               = */
/* ======================================================================= */

/* va2000hw.c — hardware init/close */
Bool    va2000Probe();
Bool    va2000InitHW();
void    va2000CloseHW();
Bool    va2000SetMode();
Bool    va2000SetBus();          /* -bus z2|z3                              */
long    va2000ModeBytes();       /* framebuffer bytes the selected mode needs */
char   *va2000BusName();

/* va2000screen.c — screen lifecycle and GC */
Bool    va2000ScreenInit();
Bool    va2000CloseScreen();
void    va2000DrawGuarantee();
Bool    va2000CreateGC();
void    va2000ValidateGC();
void    va2000DestroyGC();
void    va2000ChangeClip();
void    va2000CopyClip();
void    va2000DestroyClip();
void    va2000QueryBestSize();

/* va2000draw.c — drawing primitives */
void    va2000GetImage();
void    va2000FillSpans();
void    va2000SetSpans();
void    va2000GetSpans();
void    va2000SolidRect();
void    va2000CopyWindow();
void    va2000PaintWindow();
RegionPtr va2000CopyArea();
RegionPtr va2000CopyPlane();
void    va2000PolyPoint();
void    va2000ZeroLine();
void    va2000PolySegment();
void    va2000PolyRectangle();
void    va2000FillPolygon();
void    va2000PolyFillRect();
void    va2000PolyText8();
void    va2000ImageText8();
void    va2000FillRun();         /* va2000draw.c, used by va2000text.c      */
void    va2000CopyRun();         /* va2000draw.c, aligned pixel copy         */
void    va2000SaveAreas();       /* backing store, for mibstore              */
void    va2000RestoreAreas();

/* va2000text.c — native glyph drawing */
void    va2000PolyGlyphBlt();
void    va2000ImageGlyphBlt();

/* va2000tile.c — tile and stipple fills */
void    va2000FillTileSpans();
void    va2000FillStipSpans();
void    va2000TileBoxes();
void    va2000PutImage();
void    va2000PushPixels();

/* va2000win.c — window operations (mostly no-ops) */
Bool    va2000CreateWindow();
Bool    va2000DestroyWindow();
Bool    va2000PositionWindow();
Bool    va2000ChangeWindowAttributes();
Bool    va2000RealizeWindow();
Bool    va2000UnrealizeWindow();

/* va2000cmap.c — colormap (TrueColor: no CLUT, most are no-ops) */
Bool    va2000CreateColormap();
void    va2000DestroyColormap();
void    va2000InstallColormap();
void    va2000UninstallColormap();
int     va2000ListInstalledColormaps();
void    va2000StoreColors();
void    va2000ResolveColor();
Bool    va2000CreateDefColormap();

/* va2000cursor.c — software cursor via mi layer */
Bool    va2000RealizeCursor();
Bool    va2000UnrealizeCursor();
Bool    va2000DisplayCursor();
Bool    va2000SetCursorPosition();
void    va2000CursorLimits();
void    va2000PointerNonInterestBox();
void    va2000ConstrainCursor();
void    va2000CursorInitialize();

/* va2000pix.c — pixmap operations */
PixmapPtr va2000CreatePixmap();
Bool    va2000DestroyPixmap();
Bool    va2000RealizeFont();
Bool    va2000UnrealizeFont();

#endif /* VA2000_H */
