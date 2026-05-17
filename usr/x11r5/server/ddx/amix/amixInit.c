/*-
 * amixInit.c --
 *	Initialization functions for screen/keyboard/mouse, etc.
 *
 * Copyright (c) 1987 by the Regents of the University of California
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 *
 */

/************************************************************
Copyright 1987 by Sun Microsystems, Inc. Mountain View, CA.

                    All Rights Reserved

Permission  to  use,  copy,  modify,  and  distribute   this
software  and  its documentation for any purpose and without
fee is hereby granted, provided that the above copyright no-
tice  appear  in all copies and that both that copyright no-
tice and this permission notice appear in  supporting  docu-
mentation,  and  that the names of Sun or MIT not be used in
advertising or publicity pertaining to distribution  of  the
software  without specific prior written permission. Sun and
M.I.T. make no representations about the suitability of this
software for any purpose.  It is provided "as is" without any
express or implied warranty.

SUN DISCLAIMS ALL WARRANTIES WITH REGARD TO  THIS  SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FIT-
NESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SUN BE  LI-
ABLE  FOR  ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,  DATA  OR
PROFITS,  WHETHER  IN  AN  ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
THE USE OR PERFORMANCE OF THIS SOFTWARE.

********************************************************/

#ifndef	lint
static char sccsid[] = "%W %G Copyright 1987 Sun Micro";
#endif

#include    "amix.h"
#include    <servermd.h>
#include    "dixstruct.h"
#include    "dix.h"
#include    "opaque.h"
#include    "mipointer.h"

extern int amixMouseProc();
extern int amixKbdProc();
extern Bool amixMonoProbe(), amixMonoCreate();
extern void ProcessInputEvents();

extern void SetInputCheck();
extern char *strncpy();
extern GCPtr CreateScratchGC();

#ifdef RTG
extern Bool rtgProbe(), rtgCreate();
#endif /* RTG */

static int autoRepeatHandlersInstalled;	/* FALSE each time InitOutput called */

static Bool amixDevsProbed = FALSE;
unsigned long amixGeneration = 0;
int amixScreenIndex;
Bool FlipPixels = FALSE;

amixFbDataRec amixFbData[] = {
    amixMonoProbe, amixMonoCreate,
#ifdef TIGA
    amixMonoProbe, amixTCreate,
#endif /* TIGA */
#ifdef RTG
    rtgProbe, rtgCreate,
#endif /* RTG */
};

/*
 * NUMSCREENS is the number of supported frame buffers (i.e. the number of
 * structures in amixFbData which have an actual probeProc).
 */
#define NUMSCREENS (sizeof(amixFbData)/sizeof(amixFbData[0]))

fbFd amixFbs[MAXSCREENS];

static PixmapFormatRec	formats[] = {
    1, 1, BITMAP_SCANLINE_PAD,	/* 1-bit deep */
};
#define NUMFORMATS	(sizeof formats)/(sizeof formats[0])

/*-
 *-----------------------------------------------------------------------
 * InitOutput --
 *	Initialize screenInfo for all actually accessible framebuffers.
 *
 * Results:
 *	screenInfo init proc field set
 *
 * Side Effects:
 *	None
 *
 *-----------------------------------------------------------------------
 */

InitOutput(pScreenInfo, argc, argv)
    ScreenInfo 	  *pScreenInfo;
    int     	  argc;
    char    	  **argv;
{
    int     	  i, dev;
    static int    n = 0;        /* screen count; static so server reset works */
    static int	  setup_on_exit = 0;

    pScreenInfo->imageByteOrder = IMAGE_BYTE_ORDER;
    pScreenInfo->bitmapScanlineUnit = BITMAP_SCANLINE_UNIT;
    pScreenInfo->bitmapScanlinePad = BITMAP_SCANLINE_PAD;
    pScreenInfo->bitmapBitOrder = BITMAP_BIT_ORDER;

    pScreenInfo->numPixmapFormats = NUMFORMATS;
    for (i=0; i< NUMFORMATS; i++)
        pScreenInfo->formats[i] = formats[i];

    autoRepeatHandlersInstalled = FALSE;

    if (!amixDevsProbed)
    {
#ifdef RTG
	/*
	 * amixFbs[].type starts zeroed, which selects amixFbData[0] =
	 * amixMonoProbe.  For RTG builds the RTG entry is at index 1
	 * (index 2 if TIGA is also compiled in).  Force all heads to
	 * the RTG entry so rtgProbe/rtgCreate are called instead.
	 */
#ifdef TIGA
#define RTG_INDEX 2
#else
#define RTG_INDEX 1
#endif
	for (n = 0; n < MAXSCREENS; n++)
	    amixFbs[n].type = RTG_INDEX;
#undef RTG_INDEX
#endif /* RTG */

	for (n = 0; n < headsWanted; n++) {
	    if (!(*amixFbData[amixFbs[n].type].probeProc)(pScreenInfo, n, 0, argc, argv))
		break;
	}

	amixDevsProbed = TRUE;

	if (n == 0)
	    return;
    }

    for (i = 0; i < n; i++) {
	if (amixFbData[amixFbs[i].type].createProc)
	    (*amixFbData[amixFbs[i].type].createProc)(pScreenInfo, argc, argv);
    }

    amixInitCursor();

    ErrorF("InitOutput: done, n=%d\n", n);

    signal(SIGWINCH, SIG_IGN);
}

/*-
 *-----------------------------------------------------------------------
 * InitInput --
 *	Initialize all supported input devices...what else is there
 *	besides pointer and keyboard?
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Two DeviceRec's are allocated and registered as the system pointer
 *	and keyboard devices.
 *
 *-----------------------------------------------------------------------
 */
/*ARGSUSED*/
InitInput(argc, argv)
    int     	  argc;
    char    	  **argv;
{
    DevicePtr p, k;
    static int  zero = 0;

    ErrorF("InitInput: starting\n");

    p = AddInputDevice(amixMouseProc, TRUE);
    k = AddInputDevice(amixKbdProc, TRUE);
    if (!p || !k)
	FatalError("failed to create input devices in InitInput");

    SetTimeSinceLastInputEvent();
    RegisterPointerDevice(p);
    RegisterKeyboardDevice(k);
    miRegisterPointerDevice(screenInfo.screens[0], p);
    if (!mieqInit (k, p))
	return FALSE;

    ErrorF("InitInput: done\n");
}


static Bool
amixCloseScreen (i, pScreen)
    int		i;
    ScreenPtr	pScreen;
{
    SetupScreen(pScreen);
    Bool    ret;

    amixDisableCursor (pScreen);
    pScreen->CloseScreen = pPrivate->CloseScreen;
    ret = (*pScreen->CloseScreen) (i, pScreen);
    (void) (*pScreen->SaveScreen) (pScreen, SCREEN_SAVER_OFF);

    if (amixFbs[i].mapped)
    {
	CloseScreen(amixFbs[i].fd);
	(void) munmap(amixFbs[i].bp.bpl[0],
		      (amixFbs[i].bp.width / 8) * amixFbs[i].bp.height);

	memset(&amixFbs[i], 0, sizeof(amixFbs[i]));
    }

    xfree ((pointer) pPrivate);
    return ret;
}

Bool
amixSaveScreen (pScreen, on)
    ScreenPtr	pScreen;
    int		on;
{
    static struct scolor default_scolor[] =
    {
	{ 0x00, 0x00, 0x00, 0x00, },
	{ 0xff, 0xff, 0xff, 0xff, },
    };
    static struct scolor blank_scolor[] =
    {
	{ 0x00, 0x00, 0x00, 0x00, },
	{ 0x00, 0x00, 0x00, 0x00, },
    };

    if (on == SCREEN_SAVER_FORCER)
	SetTimeSinceLastInputEvent();
    else
    {
	if (on == SCREEN_SAVER_ON)
	{
	    if (ScreenColors(amixFbs[pScreen->myNum].fd, blank_scolor))
		return FALSE;
	}
	else
	{
	    if (ScreenColors(amixFbs[pScreen->myNum].fd, default_scolor))
		return FALSE;
	}
    }

    return TRUE;
}

/*-
 *-----------------------------------------------------------------------
 * amixScreenInit --
 *	Things which must be done for all types of frame buffers...
 *	Should be called last of all.
 *
 * Results:
 *	TRUE if successful, else FALSE
 *
 * Side Effects:
 *	Both a BlockHandler and a WakeupHandler are installed for the
 *	first screen.  Together, these handlers implement autorepeat
 *	keystrokes on the Sun.
 *
 *-----------------------------------------------------------------------
 */

Bool
amixScreenAllocate (pScreen)
    ScreenPtr	pScreen;
{
    amixScreenPtr    pPrivate;

    if (amixGeneration != serverGeneration)
    {
	amixScreenIndex = AllocateScreenPrivateIndex();
	if (amixScreenIndex < 0)
	    return FALSE;
	amixGeneration = serverGeneration;
    }
    pPrivate = (amixScreenPtr) xalloc (sizeof (amixScreenRec));
    if (!pPrivate)
	return FALSE;

    pScreen->devPrivates[amixScreenIndex].ptr = (pointer) pPrivate;
    return TRUE;
}

Bool
amixScreenInit (pScreen)
    ScreenPtr	pScreen;
{
    SetupScreen(pScreen);
    extern void   amixBlockHandler();
    extern void   amixWakeupHandler(), amixFindInputScreen();
    static ScreenPtr autoRepeatScreen;
    extern miPointerScreenFuncRec   amixPointerScreenFuncs;

    pPrivate->installedMap = 0;
    pPrivate->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = amixCloseScreen;
    pScreen->SaveScreen = amixSaveScreen;

    /*
     *	Block/Unblock handlers
     */
    if (autoRepeatHandlersInstalled == FALSE) {
	autoRepeatScreen = pScreen;
	autoRepeatHandlersInstalled = TRUE;
    }

    if (pScreen == autoRepeatScreen) {
        pScreen->BlockHandler = amixBlockHandler;
        pScreen->WakeupHandler = amixWakeupHandler;
    } else {
        pScreen->WakeupHandler = amixFindInputScreen;
    }

    if (!amixCursorInitialize (pScreen))
	miDCInitialize (pScreen, &amixPointerScreenFuncs);

    amixInitCursor ();

    return TRUE;
}
