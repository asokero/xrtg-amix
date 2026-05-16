#ifndef AMIX_RTG_H
#define AMIX_RTG_H
/*
**  $Filename: ddx/amix/rtg/rtg.h $
**
**  xamix-rtg: Generic RTG (Retargetable Graphics) DDX layer for AMIX SVR4.
**
**  Common structures and declarations shared by all RTG card drivers.
**  Each card provides its own header (e.g. va2000/va2000.h) and source
**  files, and installs its drawing functions into the ScreenRec directly.
**
**  First supported card: MNT VA2000 (16-bit TrueColor RGB565).
*/

/*
** Per-screen private record allocated for each RTG screen.
** Stored in ScreenRec->devPrivates[rtgScreenIndex].
**
** Card-specific state lives in cardPrivate, pointed to by this struct.
*/
typedef struct _rtgScreenRec {
    pointer             frameBase;      /* mmap base returned by open/mmap   */
    unsigned short     *fbBase;         /* pixel (0,0) — frameBase+fbOffset  */
    int                 fbOffset;       /* byte offset from frameBase to fb   */
    int                 width;          /* screen width  in pixels            */
    int                 height;         /* screen height in pixels            */
    int                 pitch;          /* bytes per scanline                 */
    int                 bitsPerPixel;
    int                 depth;

    ColormapPtr         installedMap;   /* NULL for TrueColor (no CLUT)       */
    Bool              (*CloseScreen)(); /* chained CloseScreen                */

    pointer             cardPrivate;    /* card-specific private data         */
} rtgScreenRec, *rtgScreenPtr;

extern int rtgScreenIndex;      /* allocated by AllocateScreenPrivateIndex() */
extern int rtgGCPrivateIndex;   /* allocated by AllocateGCPrivateIndex()     */

#define GetRTGScreen(s) \
    ((rtgScreenPtr)(s)->devPrivates[rtgScreenIndex].ptr)

/*
** GC private: clip region derived from GC clip list.
** Same pattern as TIGA's GCPRIV.
*/
#define RTGGCPRIV struct _rtg_gc_priv
RTGGCPRIV {
    RegionPtr   clip;
};

#define rtgGCClip(gc) \
    (((RTGGCPRIV *)(gc)->devPrivates[rtgGCPrivateIndex].ptr)->clip)

/*
** Entry points called from amixInit.c via amixFbData[].
*/
#ifdef RTG
extern Bool rtgProbe();
extern Bool rtgCreate();
#endif

#endif /* AMIX_RTG_H */
