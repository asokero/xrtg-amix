/*
** va2000cmap.c — Colormap management for VA2000 RTG (TrueColor, no CLUT).
**
** The VA2000 is a direct-colour framebuffer: pixel values are RGB565 words
** written directly to VRAM.  There is no hardware colour-lookup table, so:
**
**   InstallColormap   — bookkeeping only; WalkTree for TellLost/GainedMap.
**   UninstallColormap — reinstall the default colormap if needed.
**   ListInstalledColormaps — always returns 1 (exactly one map installed).
**   StoreColors       — no-op (no CLUT to update).
**   CreateColormap    — no-op (no CLUT entries to allocate).
**   DestroyColormap   — no-op.
**   ResolveColor      — round X11 16-bit RGB values to RGB565 precision.
**   CreateDefColormap — create default TrueColor map; AllocColor black/white.
*/

#include "X.h"
#include "Xproto.h"
#include "scrnintstr.h"
#include "colormapst.h"
#include "resource.h"
#include "../../amix.h"
#include "../rtg.h"
#include "va2000.h"

/* ------------------------------------------------------------------ */

Bool
va2000CreateColormap(pMap)
ColormapPtr pMap;
{
    return TRUE;
}

void
va2000DestroyColormap(pMap)
ColormapPtr pMap;
{
}

/* ------------------------------------------------------------------ */

/*
** va2000InstallColormap — make pMap the current colormap.
**
** Notifies all clients via WalkTree(TellLostMap / TellGainedMap).
** No hardware CLUT writes are needed for TrueColor.
*/
void
va2000InstallColormap(pMap)
ColormapPtr pMap;
{
    rtgScreenPtr pRTG = GetRTGScreen(pMap->pScreen);
    ColormapPtr  old  = pRTG->installedMap;

    if (pMap == old)
        return;

    if (old)
        WalkTree(pMap->pScreen, TellLostMap, (char *) &old->mid);

    pRTG->installedMap = pMap;
    WalkTree(pMap->pScreen, TellGainedMap, (char *) &pMap->mid);
}

/*
** va2000UninstallColormap — uninstall pMap.
**
** If pMap is the currently installed map and it is not the default
** colormap, reinstall the default.
*/
void
va2000UninstallColormap(pMap)
ColormapPtr pMap;
{
    rtgScreenPtr pRTG = GetRTGScreen(pMap->pScreen);

    if (pMap != pRTG->installedMap)
        return;

    if (pMap->mid != pMap->pScreen->defColormap)
    {
        ColormapPtr def = (ColormapPtr)
            LookupIDByType(pMap->pScreen->defColormap, RT_COLORMAP);
        (*pMap->pScreen->InstallColormap)(def);
    }
}

/*
** va2000ListInstalledColormaps — return the ID of the installed map.
**
** X protocol guarantees exactly one colormap is installed at all times.
*/
int
va2000ListInstalledColormaps(pScreen, pMaps)
ScreenPtr pScreen;
Colormap *pMaps;
{
    rtgScreenPtr pRTG = GetRTGScreen(pScreen);

    *pMaps = pRTG->installedMap->mid;
    return 1;
}

/*
** va2000StoreColors — no-op for TrueColor.
**
** TrueColor pixels encode colour directly; there is no CLUT to update.
*/
void
va2000StoreColors(pMap, ndef, pdefs)
ColormapPtr pMap;
int         ndef;
xColorItem *pdefs;
{
}

/* ------------------------------------------------------------------ */

/*
** va2000ResolveColor — round X11 RGB values to RGB565 hardware precision.
**
** X stores colour components as 16-bit values (0–65535).  We quantise
** each component to the number of bits available in the hardware field
** (5 for red and blue, 6 for green) then expand back to 16 bits so that
** the DIX pixel value reflects exactly what the hardware will display.
**
** The formula is the cfbResolveColor TrueColor branch, which works for
** any TrueColor visual regardless of field widths.
*/
void
va2000ResolveColor(pRed, pGreen, pBlue, pVisual)
unsigned short *pRed, *pGreen, *pBlue;
VisualPtr       pVisual;
{
    unsigned int limr, limg, limb, lim;
    int          shift;

    limr  = pVisual->redMask   >> pVisual->offsetRed;
    limg  = pVisual->greenMask >> pVisual->offsetGreen;
    limb  = pVisual->blueMask  >> pVisual->offsetBlue;
    shift = 16 - pVisual->bitsPerRGBValue;
    lim   = (1 << pVisual->bitsPerRGBValue) - 1;

    *pRed   = (unsigned short)
        ((((((*pRed   * (limr + 1)) >> 16) * 65535) / limr) >> shift)
         * 65535) / lim;
    *pGreen = (unsigned short)
        ((((((*pGreen * (limg + 1)) >> 16) * 65535) / limg) >> shift)
         * 65535) / lim;
    *pBlue  = (unsigned short)
        ((((((*pBlue  * (limb + 1)) >> 16) * 65535) / limb) >> shift)
         * 65535) / lim;
}

/* ------------------------------------------------------------------ */

/*
** va2000CreateDefColormap — create the default TrueColor colormap.
**
** Follows the cfbCreateDefColormap pattern:
**   1. Locate the visual that matches pScreen->rootVisual.
**   2. CreateColormap with AllocNone (TrueColor: client allocates cells).
**   3. AllocColor for white (all ones) and black (all zeros).
**   4. Install the new colormap.
**
** On success pScreen->whitePixel and pScreen->blackPixel are set by
** AllocColor to the nearest RGB565 representations of white and black.
*/
Bool
va2000CreateDefColormap(pScreen)
ScreenPtr pScreen;
{
    unsigned short  zero = 0, ones = 0xFFFF;
    VisualPtr       pVisual;
    ColormapPtr     cmap;

    for (pVisual = pScreen->visuals;
         pVisual->vid != pScreen->rootVisual;
         pVisual++)
        ;

    if (CreateColormap(pScreen->defColormap, pScreen, pVisual, &cmap,
                       AllocNone, 0) != Success)
        return FALSE;

    if (AllocColor(cmap, &ones, &ones, &ones,
                   &pScreen->whitePixel, 0) != Success ||
        AllocColor(cmap, &zero, &zero, &zero,
                   &pScreen->blackPixel, 0) != Success)
        return FALSE;

    (*pScreen->InstallColormap)(cmap);
    return TRUE;
}
