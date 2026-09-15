/*-
 * amixKbd.c --
 *	Functions for retrieving data from a keyboard.
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


#define NEED_EVENTS
#include "amix.h"
#include <stdio.h>
#include "Xproto.h"
#include "keysym.h"
#include "inputstr.h"
#include <signal.h>
#include <sys/ioctl.h>

extern CARD8 *amixModMap[];
extern KeySymsRec amixKeySyms[];

static void 	  amixBell();
static void 	  amixKbdCtrl();
static struct InputEvent *amixKbdGetEvents();
void	 	  amixKbdEnqueueEvent();
void 		  amixKbdProcessEvent();
int	  	  autoRepeatKeyDown = 0;
int	  	  autoRepeatReady;
long	  	  autoRepeatInitiate = 1000 * AUTOREPEAT_INITIATE;
long	  	  autoRepeatDelay = 1000 * AUTOREPEAT_DELAY;
static int	  autoRepeatFirst;
struct timeval    autoRepeatLastKeyDownTv;
struct timeval    autoRepeatDeltaTv;
static KeybdCtrl  sysKbCtrl;
/*
** amixFindInputScreen -- NOT static.
**
** amixInit.c takes this function's address:
**
**     extern void   amixWakeupHandler(), amixFindInputScreen();
**     ...
**     pScreen->WakeupHandler = amixFindInputScreen;
**
** and the definition below carries no storage class, so the file has always
** disagreed with itself.  GNU C 1.40.5 resolved that in favour of the
** definition and gave the symbol external linkage; GNU C 2.7.2.3 applies the
** `static` from the declaration, and the server stops linking:
**
**     Undefined                       first referenced
**      symbol                             in file
**     amixFindInputScreen                 ddx/amix/amixInit.o
**
** The reference in amixInit.c is the intended behaviour -- it is how the
** non-RTG screens get their wakeup handler -- so the declaration is what is
** wrong.
*/
void amixFindInputScreen();

KbPrivRec  	sysKbPriv = {
    0,				/* Fd for current device */
    amixKbdGetEvents,		/* Function to read events */
    amixKbdEnqueueEvent,	/* Function to enqueue an event */
    &sysKbCtrl,			/* Initial full duration = .25 sec. */
};

/*-
 *-----------------------------------------------------------------------
 * amixKbdProc --
 *	Handle the initialization, etc. of a keyboard.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *
 * Note:
 *	When using amixwindows, all input comes off a single fd, stored in the
 *	global windowFd.  Therefore, only one device should be enabled and
 *	disabled, even though the application still sees both mouse and
 *	keyboard.  We have arbitrarily chosen to enable and disable windowFd
 *	in the keyboard routine amixKbdProc rather than in amixMouseProc.
 *
 *-----------------------------------------------------------------------
 */
int
amixKbdProc (pKeyboard, what)
    DevicePtr	  pKeyboard;	/* Keyboard to manipulate */
    int	    	  what;	    	/* What to do to it */
{
    KbPrivPtr	  pPriv = (KbPrivPtr) pKeyboard->devicePrivate;

    switch (what) {
	case DEVICE_INIT:
	    if (pKeyboard != LookupKeyboardDevice()) {
		ErrorF ("Cannot open non-system keyboard");
		return (!Success);
	    }
	    
	    pKeyboard->devicePrivate = (pointer)&sysKbPriv;
	    pKeyboard->on = FALSE;
	    sysKbCtrl = defaultKeyboardControl;
	    sysKbPriv.ctrl = &sysKbCtrl;
	    autoRepeatKeyDown = 0;

#if 0
	    /*
	     * ensure that the keycodes on the wire are >= MIN_KEYCODE
	     */
	    if (amixKeySyms[sysKbPriv.type].minKeyCode < MIN_KEYCODE) {
		int offset = MIN_KEYCODE -amixKeySyms[sysKbPriv.type].minKeyCode;

		amixKeySyms[sysKbPriv.type].minKeyCode += offset;
		amixKeySyms[sysKbPriv.type].maxKeyCode += offset;
		sysKbPriv.offset = offset;
	    }
#endif
	    InitKeyboardDeviceStruct(
		    pKeyboard,
		    &amixKeySyms,
		    amixModMap,
		    amixBell,
		    amixKbdCtrl);
	    break;

	case DEVICE_ON:
	    {
		int i;

		for (i=0; i < screenInfo.numScreens; ++i)
		{
		    if (amixFbs[i].mapped)
			AddEnabledDevice(amixFbs[i].fd);
		}
	    }
	    pKeyboard->on = TRUE;
	    break;

	case DEVICE_CLOSE:
	case DEVICE_OFF:
	    {
		int i;

		for (i=0; i < screenInfo.numScreens; ++i)
		{
		    if (amixFbs[i].mapped)
			RemoveEnabledDevice(amixFbs[i].fd);
		}
	    }
	    pKeyboard->on = FALSE;
	    break;
    }
    return (Success);
}

/*-
 *-----------------------------------------------------------------------
 * amixBell --
 *	Ring the terminal/keyboard bell
 *
 * Results:
 *	Ring the keyboard bell for an amount of time proportional to
 *	"loudness."
 *
 * Side Effects:
 *	None, really...
 *
 *-----------------------------------------------------------------------
 */
static void
amixBell (loudness, pKeyboard)
    int	    	  loudness;	    /* Percentage of full volume */
    DevicePtr	  pKeyboard;	    /* Keyboard to ring */
{
}

/*-
 *-----------------------------------------------------------------------
 * amixKbdCtrl --
 *	Alter some of the keyboard control parameters
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Some...
 *
 *-----------------------------------------------------------------------
 */

static void
amixKbdCtrl (pKeyboard, ctrl)
    DevicePtr	  pKeyboard;	    /* Keyboard to alter */
    KeybdCtrl     *ctrl;
{
}


/*-
 *-----------------------------------------------------------------------
 * amixKbdGetEvents --
 *	Return the events waiting in the wings for the given keyboard.
 *
 * Results:
 *	A pointer to an array of InputEvents or (InputEvent *)0 if no events
 *	The number of events contained in the array.
 *	A boolean as to whether more events might be available.
 *
 * Side Effects:
 *	None.
 *-----------------------------------------------------------------------
 */
static struct InputEvent *
amixKbdGetEvents (pKeyboard, pNumEvents, pAgain)
    DevicePtr	  pKeyboard;	    /* Keyboard to read */
    int	    	  *pNumEvents;	    /* Place to return number of events */
    Bool	  *pAgain;	    /* whether more might be available */
{
    int	    	  nBytes;	    /* number of bytes of events available. */
    KbPrivPtr	  pPriv;
    static struct InputEvent	evBuf[MAXEVENTS]; /* Buffer for InputEvents */

    pPriv = (KbPrivPtr) pKeyboard->devicePrivate;
    nBytes = read (pPriv->fd, evBuf, sizeof(evBuf));

    if (nBytes < 0) {
	if (errno == EWOULDBLOCK) {
	    *pNumEvents = 0;
	    *pAgain = FALSE;
	} else {
	    Error ("Reading keyboard");
	    FatalError ("Could not read the keyboard");
	}
    } else {
	*pNumEvents = nBytes / sizeof (struct InputEvent);
	*pAgain = (nBytes == sizeof (evBuf));
    }
    return (evBuf);
}

/*-
 *-----------------------------------------------------------------------
 * amixKbdEnqueueEvent --
 *
 * Results:
 *
 * Side Effects:
 *
 * Caveat:
 *      To reduce duplication of code and logic (and therefore bugs), the
 *      amixwindows version of kbd processing (amixKbdEnqueueEventSunWin())
 *      counterfeits a firm event and calls this routine.  This
 *      couunterfeiting relies on the fact this this routine only looks at the
 *      id, time, and value fields of the firm event which it is passed.  If
 *      this ever changes, the amixKbdEnqueueEventSunWin will also have to
 *      change.
 *
 *-----------------------------------------------------------------------
 */

static xEvent	autoRepeatEvent;

void
amixKbdEnqueueEvent (pKeyboard, fe)
    DevicePtr	  pKeyboard;
    struct InputEvent	  *fe;
{
    xEvent		xE;
    KbPrivPtr		pPriv;
    int			delta;
    BYTE		key;
    CARD8		keyModifiers;
    Bool		keydown;

    key = (fe->code & 0x7F) + 1;
    keydown = (fe->code> 0x7F) ? FALSE : TRUE;

    keyModifiers = ((DeviceIntPtr)pKeyboard)->key->modifierMap[key];
    if (autoRepeatKeyDown && (keyModifiers == 0) &&
	(keydown || (key == autoRepeatEvent.u.u.detail))) {
	/*
	 * Kill AutoRepeater on any real non-modifier key down, or auto key up
	 */
	autoRepeatKeyDown = 0;
    }

    xE.u.keyButtonPointer.time = TVTOMILLI(fe->tv);
    xE.u.u.type = (keydown ? KeyPress : KeyRelease);
    xE.u.u.detail = key;

    if ((xE.u.u.type == KeyPress) && (keyModifiers == 0)) {
	/* initialize new AutoRepeater event & mark AutoRepeater on */
	autoRepeatEvent = xE;
	autoRepeatFirst = TRUE;
	autoRepeatKeyDown++;
	autoRepeatLastKeyDownTv = fe->tv;
    }

    mieqEnqueue (&xE);
}

amixEnqueueAutoRepeat ()
{
    int	oldmask;
    int	delta;

    if (sysKbPriv.ctrl->autoRepeat != AutoRepeatModeOn) {
	autoRepeatKeyDown = 0;
	return;
    }
    /*
     * Generate auto repeat event.	XXX one for now.
     * Update time & pointer location of saved KeyPress event.
     */

    delta = TVTOMILLI(autoRepeatDeltaTv);
    autoRepeatFirst = FALSE;

    /*
     * Fake a key up event and a key down event
     * for the last key pressed.
     */
    autoRepeatEvent.u.keyButtonPointer.time += delta;
    autoRepeatEvent.u.u.type = KeyRelease;
    mieqEnqueue (&autoRepeatEvent);
    autoRepeatEvent.u.u.type = KeyPress;
    mieqEnqueue (&autoRepeatEvent);
    /* Update time of last key down */
    tvplus(autoRepeatLastKeyDownTv, autoRepeatLastKeyDownTv, 
		    autoRepeatDeltaTv);

}

/*ARGSUSED*/
Bool
LegalModifier(key, pDev)
    BYTE    key;
    DevicePtr	pDev;
{
    return (TRUE);
}

static KeybdCtrl *pKbdCtrl = (KeybdCtrl *) 0;

/*ARGSUSED*/
void
amixBlockHandler(nscreen, pbdata, pptv, pReadmask)
    int nscreen;
    pointer pbdata;
    struct timeval **pptv;
    pointer pReadmask;
{
    static struct timeval artv = { 0, 0 };	/* autorepeat timeval */

    if (!autoRepeatKeyDown)
	return;

    if (pKbdCtrl == (KeybdCtrl *) 0)
	pKbdCtrl = ((KbPrivPtr) LookupKeyboardDevice()->devicePrivate)->ctrl;

    if (pKbdCtrl->autoRepeat != AutoRepeatModeOn)
	return;

    if (autoRepeatFirst == TRUE)
	artv.tv_usec = autoRepeatInitiate;
    else
	artv.tv_usec = autoRepeatDelay;
    *pptv = &artv;
}

/*ARGSUSED*/
void
amixWakeupHandler(nscreen, pbdata, err, pReadmask)
    int nscreen;
    pointer pbdata;
    unsigned long err;
    pointer pReadmask;
{
    struct timeval tv;

    if (pKbdCtrl == (KeybdCtrl *) 0)
	pKbdCtrl = ((KbPrivPtr) LookupKeyboardDevice()->devicePrivate)->ctrl;

    if (pKbdCtrl->autoRepeat != AutoRepeatModeOn)
    {
	amixFindInputScreen(nscreen, pbdata, err, (fd_set *)pReadmask);
	return;
    }

    if (autoRepeatKeyDown) {
	gettimeofday(&tv, (struct timezone *) NULL);
	tvminus(autoRepeatDeltaTv, tv, autoRepeatLastKeyDownTv);
	if (autoRepeatDeltaTv.tv_sec > 0 ||
			(!autoRepeatFirst && autoRepeatDeltaTv.tv_usec >
				autoRepeatDelay) ||
			(autoRepeatDeltaTv.tv_usec >
				autoRepeatInitiate))
		autoRepeatReady++;
    }
    
    if (autoRepeatReady)
    {
	amixEnqueueAutoRepeat ();
	autoRepeatReady = 0;
    }

    amixFindInputScreen(nscreen, pbdata, err, (fd_set *)pReadmask);
}

void
amixFindInputScreen(nscreen, pbdata, err, pReadmask)
    int nscreen;
    pointer pbdata;
    unsigned long err;
    fd_set *pReadmask;
{
    static char buffer[1024];
    extern int amixFlushScreenInputIndex;

    if (nscreen == amixFlushScreenInputIndex)
    {
	while (read(amixFbs[amixFlushScreenInputIndex].fd, buffer, sizeof(buffer)) > 0)
	    ;

	amixFlushScreenInputIndex = -1;
	return;
    }

    if (FD_ISSET(amixFbs[nscreen].fd, pReadmask))
    {
	if (amixZaphodHack(NULL, NULL, NULL, nscreen))
	{
	    time_t timing = time((time_t)0);
	    /*
	    ** As a side effect of this cursor move we will be switched
	    ** to the current input screen.
	    */
	    miPointerAbsoluteCursor(-1, -1, timing);
	}

	amixEnqueueEvents();
#if 0
	ProcessInputEvents();
#endif
    }
}
