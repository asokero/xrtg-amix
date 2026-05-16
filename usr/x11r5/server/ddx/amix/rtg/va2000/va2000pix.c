/*
** va2000pix.c — Pixmap and font operations for VA2000 RTG.
**
** Off-screen pixmaps live entirely in host memory (not in VRAM).
** The PixmapRec and its pixel data are allocated in a single xalloc
** call — the same layout as cfb — so that devPrivate.ptr = pPixmap + 1
** and devKind = padded bytes per scanline.
**
** Supported depths: 16 (native TrueColor) and 1 (bitmaps for fonts).
**
** RealizeFont / UnrealizeFont are no-ops; the VA2000 has no hardware
** font cache and font rendering is handled entirely by the mi layer.
*/

#include "X.h"
#include "scrnintstr.h"
#include "pixmapstr.h"
#include "servermd.h"
#include "dixfont.h"
#include "../../amix.h"
#include "../rtg.h"
#include "va2000.h"

/*
** va2000CreatePixmap — allocate a host-memory pixmap.
**
** PixmapRec and pixel data are allocated in one block; devPrivate.ptr
** points just past the PixmapRec header.  devKind is the scanline size
** in bytes, padded to BITMAP_SCANLINE_PAD bits (32 bits on all AMIX
** platforms).
**
** Returns NullPixmap on unsupported depth or allocation failure.
*/
PixmapPtr
va2000CreatePixmap(pScreen, width, height, depth)
ScreenPtr pScreen;
int       width;
int       height;
int       depth;
{
    PixmapPtr pPixmap;
    int       size;

    if (depth != VA2000_DEPTH && depth != 1)
        return NullPixmap;

    size    = PixmapBytePad(width, depth);
    pPixmap = (PixmapPtr) xalloc(sizeof(PixmapRec) + height * size);
    if (!pPixmap)
        return NullPixmap;

    pPixmap->drawable.type         = DRAWABLE_PIXMAP;
    pPixmap->drawable.class        = 0;
    pPixmap->drawable.pScreen      = pScreen;
    pPixmap->drawable.depth        = depth;
    pPixmap->drawable.bitsPerPixel = depth;
    pPixmap->drawable.id           = 0;
    pPixmap->drawable.serialNumber = NEXT_SERIAL_NUMBER;
    pPixmap->drawable.x            = 0;
    pPixmap->drawable.y            = 0;
    pPixmap->drawable.width        = width;
    pPixmap->drawable.height       = height;
    pPixmap->devKind               = size;
    pPixmap->refcnt                = 1;
    pPixmap->devPrivate.ptr        = (pointer)(pPixmap + 1);

    return pPixmap;
}

/*
** va2000DestroyPixmap — release a host-memory pixmap.
**
** Decrements the reference count; frees only when it reaches zero.
** Because the header and data share one allocation, a single xfree
** releases both.
*/
Bool
va2000DestroyPixmap(pPixmap)
PixmapPtr pPixmap;
{
    if (--pPixmap->refcnt)
        return TRUE;
    xfree((pointer) pPixmap);
    return TRUE;
}

/*
** va2000RealizeFont — no hardware font cache; always succeeds.
*/
Bool
va2000RealizeFont(pScreen, pFont)
ScreenPtr pScreen;
FontPtr   pFont;
{
    return TRUE;
}

/*
** va2000UnrealizeFont — no hardware font cache; always succeeds.
*/
Bool
va2000UnrealizeFont(pScreen, pFont)
ScreenPtr pScreen;
FontPtr   pFont;
{
    return TRUE;
}
