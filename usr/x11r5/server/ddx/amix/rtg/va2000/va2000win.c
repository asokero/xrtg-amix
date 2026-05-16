/*
** va2000win.c — Window lifecycle hooks for VA2000 RTG driver.
**
** The VA2000 is a plain framebuffer with no hardware window support.
** All ScreenRec window hooks are no-ops that return TRUE.
**
** PaintWindowBackground / PaintWindowBorder are in va2000draw.c.
** CopyWindow is in va2000draw.c.
*/

#include "X.h"
#include "windowstr.h"
#include "../../amix.h"
#include "../rtg.h"
#include "va2000.h"

Bool
va2000CreateWindow(pWin)
WindowPtr pWin;
{
    return TRUE;
}

Bool
va2000DestroyWindow(pWin)
WindowPtr pWin;
{
    return TRUE;
}

Bool
va2000PositionWindow(pWin, x, y)
WindowPtr pWin;
int       x, y;
{
    return TRUE;
}

Bool
va2000ChangeWindowAttributes(pWin, mask)
WindowPtr     pWin;
unsigned long mask;
{
    return TRUE;
}

Bool
va2000RealizeWindow(pWin)
WindowPtr pWin;
{
    return TRUE;
}

Bool
va2000UnrealizeWindow(pWin)
WindowPtr pWin;
{
    return TRUE;
}
