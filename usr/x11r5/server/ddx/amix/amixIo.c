/*-
 * amixIo.c --
 *	Functions to handle input from the keyboard and mouse.
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
software for any purpose. It is provided "as is" without any
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
#include    "opaque.h"
#include    "rtg/va2000/va2000.h"

int	    	lastEventTime = 0;
extern int      screenIsSaved;
extern void	SaveScreens();
int AmixIndex;

/*-
 *-----------------------------------------------------------------------
 * TimeSinceLastInputEvent --
 *	Function used for screensaver purposes by the os module.
 *
 * Results:
 *	The time in milliseconds since there last was any
 *	input.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
int
TimeSinceLastInputEvent()
{
    struct timeval	now;

    gettimeofday (&now, (struct timezone *)0);

    if (lastEventTime == 0) {
	lastEventTime = TVTOMILLI(now);
    }
    return TVTOMILLI(now) - lastEventTime;
}

/*-
 *-----------------------------------------------------------------------
 * ProcessInputEvents --
 *	Retrieve all waiting input events and pass them to DIX in their
 *	correct chronological order. Only reads from the system pointer
 *	and keyboard.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Events are passed to the DIX layer.
 *
 *-----------------------------------------------------------------------
 */
void
ProcessInputEvents ()
{
    mieqProcessInputEvents ();
    miPointerUpdate ();
}

/*
 *-----------------------------------------------------------------------
 * amixEnqueueEvents
 */

#define	INPBUFSIZE	128
#define	FAKEINPBUFSIZE	(INPBUFSIZE*2)

amixEnqueueEvents ()
{
    DevicePtr		pPointer, pKeyboard;
    struct inputevent	amixevents[INPBUFSIZE];
    struct InputEvent	AmixEvents[FAKEINPBUFSIZE];
    register struct InputEvent *ae = AmixEvents, *aeL;
    int			n, to, from;
    KbPrivPtr	  pPriv;
    unsigned char newQualifiers, currentQualifiers;
    static unsigned char oldQualifiers;

    pPointer = LookupPointerDevice();
    pKeyboard = LookupKeyboardDevice();
    pPriv = (KbPrivPtr) pKeyboard->devicePrivate;

    if ((n=read(pPriv->fd, amixevents, INPBUFSIZE*sizeof amixevents[0])) <
	0 && errno != EWOULDBLOCK)
    {
	/*
	 * Error reading events; should do something. XXX
	 */
	ErrorF("ProcessInputEvents: read(windowFd)  n=%d\n",n);
	return;
    }

    n /= sizeof(amixevents[0]);

    /*
    ** Convert the screen events to fakescreen events.
    */

    for (to=from=0; to < FAKEINPBUFSIZE && from < n; to++, from++)
    {
	/*
	** Check to see the qualifiers haven't spoofed us.  This can
	** happen if someone holds down (say) SHIFT while switching
	** screens.  When They switch back the SHIFT KEY will still
	** look to us as if it is down even if it isn't
	*/

	currentQualifiers = (amixevents[from].qualifiers&0xFF);
	newQualifiers = oldQualifiers ^ currentQualifiers;

#ifdef DEBUG_QUALIFIERS
	if (newQualifiers)
	{
	    printf("type=0x%x, code=0x%x, current=0x%x, old=0x%x, new=0x%x\n",
		   amixevents[from].type, amixevents[from].code,
		   currentQualifiers, oldQualifiers, newQualifiers);
	}
	else
	{
	    printf("type=0x%x, code=0x%x, current=0x%x\n",
		   amixevents[from].type, amixevents[from].code,
		   currentQualifiers);
	}
#endif

	if (newQualifiers
#define BUGGY_MOUSE_QUALIFIERS
#ifdef BUGGY_MOUSE_QUALIFIERS
	    && amixevents[from].type == 1
#endif
	    )
	{
	    int i, code;

	    for (i=0; i < 8; i++)
	    {
		if (newQualifiers&1)
		{
		    code = ((currentQualifiers&(1<<i)) ? 0x60 : 0xE0) + i;

		    /*
		    ** If the qualifier we are looking at has the same
		    ** code as the current event (and the current event
		    ** is a key event) then defer it til later so
		    ** Qualifiers that mysteriously appeared will be
		    ** dealt with first.
		    */
		    if (amixevents[from].code != code)
		    {
			/*
			** Qualifier `i' has changed state in addition
			** to the keycode for this amixevent, so fake a
			** state change.
			*/
#ifdef DEBUG_QUALIFIERS
			printf("  code=0x%x (added) type=0x%x\n", code,
			       amixevents[from].type);
#endif
			AmixEvents[to].type = 1;
			AmixEvents[to].class = 0;
			AmixEvents[to].code = code;
			AmixEvents[to].qualifiers = 0;
			gettimeofday(&AmixEvents[to].tv, (struct timezone *)0);
			to++;
		    }
#ifdef DEBUG_QUALIFIERS
		    else
			printf("  code=0x%x (deferred)\n", code);
#endif
		}

		if (to >= FAKEINPBUFSIZE || !(newQualifiers >>= 1))
		    break;
	    }

	    if (to >= FAKEINPBUFSIZE)
		break;

	    oldQualifiers = currentQualifiers;
	}

#ifdef DEBUG_QUALIFIERS
	if (oldQualifiers ^ currentQualifiers)
	{
	    printf("  code=0x%x type=0x%x\n", amixevents[from].code,
		   amixevents[from].type);
	}
#endif
	/*
	** Copy real input events structure to
	** fake structure and add timestamp.
	*/
	AmixEvents[to].type = amixevents[from].type;
	AmixEvents[to].class = amixevents[from].class;
	AmixEvents[to].code = amixevents[from].code;
	AmixEvents[to].qualifiers = amixevents[from].qualifiers;
	gettimeofday(&AmixEvents[to].tv, (struct timezone *)0);
    }

    if (to <= 0)		/* No events ... bail */
	return;

    for (aeL = &AmixEvents[to];  ae < aeL; ae++)
    {
	unsigned short key_code = ae->code & 0x7F;

	lastEventTime = TVTOMILLI(ae->tv);

	/*
	** <HACK> This little check strips the Alt-Function Key
	** <HACK> sequences that are used by the system to switch
	** <HACK> from screen to screen.
	*/

	if (ae->qualifiers & QUALIFIERS_ALT) 
	{
	    if (key_code == ESCAPE_KEY ||
		(key_code >= FUNCTION_KEY_1 && key_code <= PRINT_SCREEN_KEY))
	    {
		continue;
	    }
	}

	/*
	** Call the appropriate processing routine per event.
	*/

	if (ae->type == MOUSE_MOVE_EVENT ||
	    (key_code >= LEFT_MOUSE_BUTTON && key_code <= RIGHT_MOUSE_BUTTON))
	{
	    amixMouseEnqueueEvent (pPointer, ae);
	}
	else
	{
	    amixKbdEnqueueEvent (pKeyboard, ae);
	}
    }
}

/*-
 *-----------------------------------------------------------------------
 * SetTimeSinceLastInputEvent --
 *	Set the lastEventTime to now.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	lastEventTime is altered.
 *
 *-----------------------------------------------------------------------
 */
void
SetTimeSinceLastInputEvent()
{
    struct timeval now;

    gettimeofday (&now, (struct timezone *)0);
    lastEventTime = TVTOMILLI(now);
}

/*
 * DDX - specific abort routine.  Called by AbortServer().
 */
void
AbortDDX()
{
    int		i;
    ScreenPtr	pScreen;

    for (i = 0; i < screenInfo.numScreens; i++)
    {
	pScreen = screenInfo.screens[i];
	(*pScreen->SaveScreen) (pScreen, SCREEN_SAVER_OFF);
	amixDisableCursor (pScreen);
    }
}

/* Called by GiveUp(). */
void
ddxGiveUp()
{
    AbortDDX ();
}

int
ddxProcessArgument (argc, argv, i)
    int	argc;
    char *argv[];
    int	i;
{
    extern void UseMsg();
    extern Bool ActiveZaphod;
    extern Bool FlipPixels;
#ifdef TIGA
    extern char *tigagm;
    extern int tigamode;
    extern char *TigaBoard;
#ifndef DONT_PANIC
    extern Bool PanicMode;
    extern long UseMask;
#endif
#endif /* TIGA */

    if (strcmp (argv[i], "-ar1") == 0) {	/* -ar1 int */
	if (++i >= argc) UseMsg ();
	autoRepeatInitiate = 1000 * (long)atoi(argv[i]);
	return 2;
    }
    if (strcmp (argv[i], "-ar2") == 0) {	/* -ar2 int */
	if (++i >= argc) UseMsg ();
	autoRepeatDelay = 1000 * (long)atoi(argv[i]);
	return 2;
    }
    if (strcmp (argv[i], "-debug") == 0) {	/* -debug */
	return 1;
    }
    if (strcmp (argv[i], "-mono") == 0) {	/* -mono */
	amixFbs[AmixIndex].type = NATIVE_MONO;
	return 1;
    }
    if (strcmp (argv[i], "-zaphod") == 0) {	/* -zaphod */
	ActiveZaphod = FALSE;
	return 1;
    }
    if (strcmp (argv[i], "-flipPixels") == 0) {	/* -flipPixels */
	FlipPixels = TRUE;
	return 1;
    }
    if (!strcmp(argv[i], "-xresolution")) {	/* -resolution int */
	if (++i >= argc) UseMsg ();
	XmonitorResolution = atoi(argv[i]);
	return 2;
    }
    if (!strcmp(argv[i], "-yresolution")) {	/* -resolution int */
	if (++i >= argc) UseMsg ();
	YmonitorResolution = atoi(argv[i]);
	return 2;
    }
    if (!strcmp(argv[i], "-width")) {	/* screen width */
	if (++i >= argc) UseMsg ();
	amixFbs[AmixIndex].scrtype.dispx = atoi(argv[i]);
	return 2;
    }
    if (!strcmp(argv[i], "-height")) {	/* screen height */
	if (++i >= argc) UseMsg ();
	amixFbs[AmixIndex].scrtype.dispy = atoi(argv[i]);
	return 2;
    }
    if (!strcmp(argv[i], "-depth")) {	/* screen depth */
	if (++i >= argc) UseMsg ();
	amixFbs[AmixIndex].scrtype.dispz = atoi(argv[i]);
	return 2;
    }
    if (!strcmp(argv[i], "-mode")) {	/* RTG mode: WxH (e.g. 1024x768) */
	if (++i >= argc) UseMsg ();
	if (!va2000SetMode(argv[i])) {
	    ErrorF("Unknown -mode: %s\n", argv[i]);
	    ErrorF("Supported modes: 640x480 800x600 1024x768 1280x720 1280x1024 1920x1080\n");
	    UseMsg();
	}
	return 2;
    }
    if (!strcmp(argv[i], "-type")) {	/* screen type */
	if (++i >= argc) UseMsg ();
	amixFbs[AmixIndex].scrtype.type = atoi(argv[i]);
	return 2;
    }
    if (!strcmp(argv[i], "-flags")) {	/* screen flags */
	if (++i >= argc) UseMsg ();
	amixFbs[AmixIndex].scrtype.flags = atoi(argv[i]);
	return 2;
    }
    if (!strcmp(argv[i], "-head")) {	/* current head to set */
	extern struct scrtype DefaultScrType;

	if (++i >= argc) UseMsg ();
	AmixIndex = atoi(argv[i]);
	amixFbs[AmixIndex].scrtype = DefaultScrType;
	return 2;
    }
    if (!strcmp(argv[i], "-heads")) {	/* Zaphod heads wanted */
	if (++i >= argc) UseMsg ();
	headsWanted = atoi(argv[i]);
	if (headsWanted < 1 || headsWanted > MAXSCREENS)
	{
	    ErrorF("headsWanted must be between 1 and 10\n");
	    UseMsg ();
	}

	return 2;
    }
    if (!strcmp(argv[i], "-spread_heads")) {	/* inc group num after open */
	spreadHeads = TRUE;
	return 1;
    }
    if (!strcmp(argv[i], "-group")) {	/* start screens on this group */
	if (++i >= argc)
	    UseMsg ();
	group = atoi(argv[i]);
	return 2;
    }
#ifdef TIGA
    if ((strcmp(argv[i], "-gfxmgr"))==0)
    {
	if (++i >= argc)
	    UseMsg ();

	tigagm = argv[i];
	return 2;
    }

    if ((strcmp(argv[i], "-tigamode"))==0)
    {
	if (++i >= argc)
	    UseMsg ();

	tigamode = atoi(argv[i]);
 	return 2;
    }

    if (!strncmp(argv[i], "-res", 4))
    {
	amixFbs[AmixIndex].type = DMI_RESOLVER;

	if (argv[i][4] != '\0')
	    TigaBoard = &argv[i][1];
	else
	    TigaBoard = "res0";

	return 1;
    }

#ifndef DONT_PANIC
    if((strcmp(argv[i],"-panic"))==0)
    {
	PanicMode = TRUE;
	if(i+2 < argc && strcmp(argv[i+1], "mask") == 0)
	{
	    UseMask = strtol(argv[i+2], (char **)0, 0);
	    return 3;
	}

	return 1;
    }
#endif
#endif /* TIGA */

    return 0;
}

void
ddxUseMsg()
{
    ErrorF("-ar1 int               set autorepeat initiate time\n");
    ErrorF("-ar2 int               set autorepeat interval time\n");
    ErrorF("-debug                 disable non-blocking console mode\n");
    ErrorF("-mono                  force monochrome-only screen\n");
    ErrorF("-zaphod                disable active Zaphod mode\n");
    ErrorF("-heads int		   number of Zaphod screens to open\n");
    ErrorF("-spread_heads	   put zaphod heads on different groups\n");
    ErrorF("-xresolution int       x pixels per inch.\n");
    ErrorF("-yresolution int       y pixels per inch.\n");
    ErrorF("-flags int             screen flags.\n");
    ErrorF("-type int              screen type.\n");
    ErrorF("-width int             screen width in pixels.\n");
    ErrorF("-height int            screen height in pixels.\n");
    ErrorF("-depth int             screen depth in planes.\n");
    ErrorF("-mode WxH              RTG video mode (e.g. 800x600, 1024x768)\n");
    ErrorF("-group int		   screen group\n");
#ifdef TIGA
    ErrorF("-res<#>                Use /dev/dmi<#> as display.\n");
    ErrorF("-gfxmgr filename       Tiga graphics manager.\n");
    ErrorF("-tigamode int          Set tiga mode.\n");
#ifndef DONT_PANIC
    ErrorF("-panic [mask int]      Debugging Stuff.\n");
#endif
#endif /* TIGA */
}
