/*
** va2000cursor.c — Software cursor for VA2000 RTG via mi layer.
**
** The VA2000 has no hardware sprite.  va2000CursorInitialize() delegates
** to miDCInitialize(), which installs the miSprite/miDC software cursor
** machinery and overrides all ScreenRec cursor entries.
**
** The remaining functions are stubs installed as initial placeholders in
** the ScreenRec before miDCInitialize() is called.  In practice they are
** never reached; miDCInitialize() overwrites every ScreenRec slot.
*/

#include "X.h"
#include "Xmd.h"
#include "cursorstr.h"
#include "scrnintstr.h"
#include "mipointer.h"
#include "../../amix.h"
#include "../rtg.h"
#include "va2000.h"

extern miPointerScreenFuncRec amixPointerScreenFuncs;
extern void miRecolorCursor();

/* ------------------------------------------------------------------ */
/* Stubs — overridden by miDCInitialize before any cursor is drawn     */

Bool
va2000RealizeCursor(pScreen, pCursor)
ScreenPtr pScreen;
CursorPtr pCursor;
{
    return TRUE;
}

Bool
va2000UnrealizeCursor(pScreen, pCursor)
ScreenPtr pScreen;
CursorPtr pCursor;
{
    return TRUE;
}

Bool
va2000DisplayCursor(pScreen, pCursor)
ScreenPtr pScreen;
CursorPtr pCursor;
{
    return TRUE;
}

Bool
va2000SetCursorPosition(pScreen, newx, newy, genEvent)
ScreenPtr    pScreen;
unsigned int newx, newy;
Bool         genEvent;
{
    return TRUE;
}

void
va2000CursorLimits(pScreen, pCursor, pHotBox, pTopLeftBox)
ScreenPtr pScreen;
CursorPtr pCursor;
BoxPtr    pHotBox;
BoxPtr    pTopLeftBox;
{
    pTopLeftBox->x1 = max(pHotBox->x1, 0);
    pTopLeftBox->y1 = max(pHotBox->y1, 0);
    pTopLeftBox->x2 = min(pHotBox->x2, (int) pScreen->width);
    pTopLeftBox->y2 = min(pHotBox->y2, (int) pScreen->height);
}

void
va2000PointerNonInterestBox(pScreen, pBox)
ScreenPtr pScreen;
BoxPtr    pBox;
{
}

void
va2000ConstrainCursor(pScreen, pBox)
ScreenPtr pScreen;
BoxPtr    pBox;
{
}

/* ------------------------------------------------------------------ */

/*
** va2000CursorInitialize — install the mi software cursor.
**
** Calls miDCInitialize() to wire the miSprite/miDC sprite layer into the
** ScreenRec.  After this returns, RealizeCursor … ConstrainCursor all
** point to mi functions and the stubs above are unreachable.
**
** Also installs the remaining cursor entries so the ScreenRec is fully
** populated before miDCInitialize() performs its own override.
*/
void
va2000CursorInitialize(pScreen)
ScreenPtr pScreen;
{
    pScreen->RealizeCursor         = va2000RealizeCursor;
    pScreen->UnrealizeCursor       = va2000UnrealizeCursor;
    pScreen->DisplayCursor         = va2000DisplayCursor;
    pScreen->SetCursorPosition     = va2000SetCursorPosition;
    pScreen->CursorLimits          = va2000CursorLimits;
    pScreen->PointerNonInterestBox = va2000PointerNonInterestBox;
    pScreen->ConstrainCursor       = va2000ConstrainCursor;
    pScreen->RecolorCursor         = miRecolorCursor;

    miDCInitialize(pScreen, &amixPointerScreenFuncs);
}
