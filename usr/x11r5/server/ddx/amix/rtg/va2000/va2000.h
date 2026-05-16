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

/* ======================================================================= */
/* = Default video mode: 800x600x16                                      = */
/* ======================================================================= */

#define VA2000_WIDTH        800
#define VA2000_HEIGHT       600
#define VA2000_BPP          16
#define VA2000_DEPTH        16
#define VA2000_PITCH        (VA2000_WIDTH * 2)  /* 1600 bytes per scanline  */

/* ======================================================================= */
/* = Hardware registers (byte offsets from mmap base)                   = */
/* ======================================================================= */

/* Screen geometry — written before activating RTG mode */
#define VA2000_REG_WIDTH        0x06    /* display width  in pixels         */
#define VA2000_REG_HEIGHT       0x08    /* display height in pixels         */
#define VA2000_REG_UNKNOWN_0E   0x0e    /* set to 1 during init             */
#define VA2000_REG_PITCH        0x58    /* framebuffer pitch in pixels      */

/* Capture / RTG mode select */
#define VA2000_REG_CAPTURE      0x4e    /* 0=RTG active, 1=passthrough      */
#define VA2000_CAPTURE_RTG      0
#define VA2000_CAPTURE_PASSTHRU 1

/* Modeline registers — horizontal timing */
#define VA2000_REG_HTOTAL       0x70    /* horizontal total                 */
#define VA2000_REG_HSYNC_START  0x72    /* horizontal sync start            */
#define VA2000_REG_HSYNC_END    0x74    /* horizontal sync end              */

/* Modeline registers — vertical timing */
#define VA2000_REG_VTOTAL       0x76    /* vertical total                   */
#define VA2000_REG_VSYNC_START  0x78    /* vertical sync start              */
#define VA2000_REG_VSYNC_END    0x7a    /* vertical sync end                */

#define VA2000_REG_UNKNOWN_7C   0x7c    /* set to 1 during init             */

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
    ((unsigned short *)(fbbase) + (y) * (VA2000_PITCH / 2) + (x))

/*
** Write a register in the control area (before the framebuffer).
*/
#define VA2000_WRITEREG(base, reg, val) \
    (*((volatile unsigned short *)((char *)(base) + (reg))) = (unsigned short)(val))

/* ======================================================================= */
/* = Card-specific screen private                                        = */
/* ======================================================================= */

typedef struct _va2000ScreenRec {
    int             fd;         /* open file descriptor for VA2000_DEV      */
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

/* va2000screen.c — screen lifecycle and GC */
Bool    va2000ScreenInit();
Bool    va2000CloseScreen();
Bool    va2000CreateGC();
void    va2000ValidateGC();
void    va2000DestroyGC();
void    va2000ChangeClip();
void    va2000CopyClip();
void    va2000DestroyClip();
void    va2000QueryBestSize();

/* va2000draw.c — drawing primitives */
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
