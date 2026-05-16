/*-
 * amixCursor.c --
 *	Functions for maintaining a software cursor
 *
 */

#define NEED_EVENTS
#include    "amix.h"
#include    <windowstr.h>
#include    <regionstr.h>
#include    <dix.h>
#include    <dixstruct.h>
#include    <opaque.h>

#include    <servermd.h>
#include    "mipointer.h"
#include    "cursorstr.h"

Bool
amixCursorInitialize (pScreen)
    ScreenPtr	pScreen;
{
    SetupScreen(pScreen);

#ifdef TIGA
    if (amixFbs[pScreen->myNum].type == DMI_RESOLVER)
    {
	tigCursorInitialize(pScreen);
	return TRUE;
    }
#endif /* TIGA */

    return FALSE;
}

amixDisableCursor(pScreen)
    ScreenPtr	pScreen;
{
}

void
amixInitCursor ()
{
}
