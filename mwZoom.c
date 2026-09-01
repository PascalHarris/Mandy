/*****
 * mwZoom.c
 *
 *		Everything that changes the fractal's visible region or the
 *		window's own shape: marquee selection and the zoom-to-
 *		selection feature it drives, resizing the window itself via
 *		its grow box, and keyboard zoom (+/-). All three end up
 *		adjusting gView (see mwWindow.h) and/or the window's actual
 *		size, then calling RenderFractalOffscreen().
 *
 *		REQUIRES two DLOG/DITL resource pairs - resources, not
 *		something this file can create:
 *
 *		  "Zoom Confirmation", ID 128 (kZoomConfirmDialogID). As
 *		  actually built:
 *
 *		    DITL 128, three items in this order:
 *		      1. StaticText, "Zoom to the selected area?"
 *		      2. Button,     "Zoom"
 *		      3. Button,     "Cancel"
 *
 *		    (This is a different order than originally specified -
 *		    Zoom/Cancel/text - which put the two buttons first;
 *		    building the text label first, as ResEdit naturally
 *		    encourages, is just as fine, since ConfirmZoom() finds
 *		    "Zoom" by its actual title text, not by item number.)
 *
 *		  "Insufficient Memory", ID 129 (kInsufficientMemoryDialogID),
 *		  shown by TrackWindowResize() when the requested size looks
 *		  too large to allocate. Same shape:
 *
 *		    DITL 129, three items, order doesn't matter (see below):
 *		      A StaticText, something like "There isn't enough memory
 *		        for that size. Use the largest size that fits instead?"
 *		      A Button, "OK"
 *		      A Button, "Cancel"
 *
 *		  Item order genuinely doesn't matter for either dialog, for
 *		  either DITL: ConfirmZoom()/ConfirmUseLargestSize() both find
 *		  their affirmative button by its actual title text
 *		  (DialogItemTitleIs()), not by which item number it happens
 *		  to be - real testing already caught this assumption failing
 *		  once (the Zoom Confirmation DITL's buttons ended up numbered
 *		  the other way around from what was first assumed), so
 *		  neither dialog depends on getting it "right" a particular
 *		  way. What does matter: exactly one item's title must read
 *		  "Zoom" (for DITL 128) or "OK" (for DITL 129) - kZoomButtonTitle/
 *		  kOKButtonTitle below - and there must be exactly two buttons
 *		  total in each, since the tracking loop that waits for a click
 *		  needs to know how many item numbers to watch for.
 *
 *		  Both DLOGs: referencing their own DITL, procID dBoxProc (a
 *		  plain box with no title bar - not a document window, and not
 *		  one of ResEdit's numbered/custom WDEF slots), goAway off
 *		  (dismissed by their own buttons, not a close box). The DLOG's
 *		  own "initially visible" flag doesn't matter either way - both
 *		  confirmation functions call ShowWindow() themselves rather
 *		  than relying on it, after real testing found that flag
 *		  unchecked on the first dialog and it consequently never
 *		  appearing at all.
 *
 *****/
#include "mwZoom.h"
#include "mwWindow.h"
#include <QDOffscreen.h>
#ifndef _Quickdraw_
#include <Quickdraw.h>
#endif
#ifndef _Dialogs_
#include <Dialogs.h>
#endif
#ifndef _Memory_
#include <Memory.h>
#endif

extern	WindowPtr	mwWindow;
extern	Boolean		gHasColorQD;	/* set once in MandyWindow.c's InitMacintosh() */

/* Below this, in either dimension, a marquee is treated as an
   accidental click-drag rather than a deliberate selection - avoids
   popping the confirmation dialog for a one-or-two-pixel jiggle. */
#define kMinimumMarqueeSize		8

/* The aspect ratio both the marquee and the window's own grow box are
   locked to (see ConstrainToAspectRatio(), TrackMarqueeAndZoom(), and
   TrackWindowResize()). Kept as two separate numbers rather than a
   single named "5x3" constant so a future change to this ratio -
   already revised once, from an initial 4:3 - is a one-line edit here
   rather than a rename of ConstrainToAspectRatio() and every call to
   it. This is also now the window's own actual shape once resized via
   its grow box (see TrackWindowResize()), rather than an aspiration
   the fixed-size window didn't yet match. */
#define kAspectRatioNumerator	5
#define kAspectRatioDenominator	3

#define kZoomConfirmDialogID	128
#define kTextItem				1
#define kFirstButtonItem		2
#define kSecondButtonItem		3
#define kZoomButtonTitle		"\pZoom"

/* "Insufficient Memory" dialog - see the resource requirement
   documented above TrackWindowResize(). Same item layout as the Zoom
   Confirmation dialog (text, then two buttons), so kTextItem/
   kFirstButtonItem/kSecondButtonItem above apply to this one too -
   it's just a different DLOG/DITL resource ID. */
#define kInsufficientMemoryDialogID	129
#define kOKButtonTitle				"\pOK"

/* TrackWindowResize()'s size bounds - see there for reasoning.
   100x60 is exactly 5:3, matching kAspectRatioNumerator/Denominator,
   so the minimum itself is never a degenerate, off-ratio shape. */
#define kMinimumWindowWidth		100
#define kMinimumWindowHeight	60

static void    ConstrainToAspectRatio(Rect *r, Point anchor, Point current, short ratioNumerator, short ratioDenominator);
static void    ClampPointToImageBounds(Point *p);
static Boolean PascalStringsEqual(const unsigned char *a, const unsigned char *b);
static Boolean DialogItemTitleIs(DialogPtr dialog, short itemNumber, const unsigned char *expectedTitle);
static Boolean ConfirmZoom(void);
static Boolean ConfirmUseLargestSize(void);
static void    FindLargestSizeFittingMemory(short maxWidth, short maxHeight, long availableBytes, short *outWidth, short *outHeight);

/* TrackMarqueeAndZoom()
   See mwZoom.h. */
void TrackMarqueeAndZoom(Point globalMouseDownPoint) {
	Point	localAnchor, currentPoint;
	Rect	marqueeRect, previousRect;
	Boolean	haveDrawnAFrame = false;
	GrafPtr	savedPort;
	
	GetPort(&savedPort);
	SetPort(mwWindow);
	
	localAnchor = globalMouseDownPoint;
	GlobalToLocal(&localAnchor);
	ClampPointToImageBounds(&localAnchor);
	
	PenMode(patXor);
	
	while (StillDown()) {
		GetMouse(&currentPoint);
		ClampPointToImageBounds(&currentPoint);
		
		ConstrainToAspectRatio(&marqueeRect, localAnchor, currentPoint, kAspectRatioNumerator, kAspectRatioDenominator);
		
		if (!haveDrawnAFrame || !EqualRect(&marqueeRect, &previousRect)) {
			if (haveDrawnAFrame)
				FrameRect(&previousRect);	/* erase the previous frame */
			
			FrameRect(&marqueeRect);
			previousRect    = marqueeRect;
			haveDrawnAFrame = true;
		}
	}
	
	if (haveDrawnAFrame)
		FrameRect(&previousRect);	/* erase the final frame */
	
	PenMode(patCopy);
	SetPort(savedPort);
	
	if (!haveDrawnAFrame)
		return;
	
	if ((marqueeRect.right - marqueeRect.left) < kMinimumMarqueeSize ||
			(marqueeRect.bottom - marqueeRect.top) < kMinimumMarqueeSize)
		return;
	
	{
		FractalView	candidate;
		double		dRe1, dIm1, dRe2, dIm2;
		
		MapPixelToComplexPlane(marqueeRect.left,  marqueeRect.top,    &dRe1, &dIm1);
		MapPixelToComplexPlane(marqueeRect.right, marqueeRect.bottom, &dRe2, &dIm2);
		
		candidate.centreRe    = (dRe1 + dRe2) / 2.0;
		candidate.centreIm    = (dIm1 + dIm2) / 2.0;
		candidate.halfWidthRe = (dRe2 - dRe1) / 2.0;
		if (candidate.halfWidthRe < 0.0)
			candidate.halfWidthRe = -candidate.halfWidthRe;
		candidate.halfWidthRe = ClampHalfWidthRe(candidate.halfWidthRe);
		
		if (ConfirmZoom()) {
			gView = candidate;
			RenderFractalOffscreen();
			DrawContent(((WindowPeek) mwWindow)->hilited);
		}
	}
}

/* ConstrainToAspectRatio()
   Recomputes the shorter of the two dragged dimensions from the
   longer one, at ratioNumerator:ratioDenominator, on every call -
   rather than letting the marquee free-drag and only checking its
   ratio once at mouse-up - so the frame drawn during tracking always
   already reflects the constrained shape, not a free-form rectangle
   that only snaps to the right proportions after the fact. Preserves
   the drag direction (sign) of whichever dimension gets recomputed, so
   dragging up-and-left versus down-and-right both anchor correctly at
   `anchor`. r is always returned normalized (left<right, top<bottom)
   regardless of which direction anchor and current imply, since
   FrameRect() and the rest of this file assume that. */
static void ConstrainToAspectRatio(Rect *r, Point anchor, Point current, short ratioNumerator, short ratioDenominator) {
	short	draggedWidth  = current.h - anchor.h;
	short	draggedHeight = current.v - anchor.v;
	short	absWidth  = (draggedWidth  < 0) ? -draggedWidth  : draggedWidth;
	short	absHeight = (draggedHeight < 0) ? -draggedHeight : draggedHeight;
	short	newAbsWidth, newAbsHeight;
	
	if ((long) absWidth * ratioDenominator > (long) absHeight * ratioNumerator) {
		newAbsWidth  = absWidth;
		newAbsHeight = (short) (((long) absWidth * ratioDenominator) / ratioNumerator);
	} else {
		newAbsHeight = absHeight;
		newAbsWidth  = (short) (((long) absHeight * ratioNumerator) / ratioDenominator);
	}
	
	draggedWidth  = (draggedWidth  < 0) ? -newAbsWidth  : newAbsWidth;
	draggedHeight = (draggedHeight < 0) ? -newAbsHeight : newAbsHeight;
	
	SetRect(r, anchor.h, anchor.v, anchor.h + draggedWidth, anchor.v + draggedHeight);
	
	if (r->left > r->right) {
		short temp = r->left;
		r->left  = r->right;
		r->right = temp;
	}
	if (r->top > r->bottom) {
		short temp = r->top;
		r->top    = r->bottom;
		r->bottom = temp;
	}
}

/* ClampPointToImageBounds()
   Keeps a tracked point within the rendered image's own bounds, so
   dragging the mouse outside the window (or into its title bar)
   doesn't extend the marquee - and the eventual zoom candidate it
   produces - beyond the area that was actually rendered. */
static void ClampPointToImageBounds(Point *p) {
	short imageWidth, imageHeight;
	
	GetFractalResolution(&imageWidth, &imageHeight);
	
	if (p->h < 0)           p->h = 0;
	if (p->h > imageWidth)  p->h = imageWidth;
	if (p->v < 0)           p->v = 0;
	if (p->v > imageHeight) p->v = imageHeight;
}

/* PascalStringsEqual()
   A plain byte-by-byte comparison of two Pascal strings (length byte
   followed by that many characters) - used instead of the Script
   Manager's EqualString() so ConfirmZoom() doesn't add a dependency on
   another call this project has never used before and hasn't proven
   links here, for a comparison this simple. */
static Boolean PascalStringsEqual(const unsigned char *a, const unsigned char *b) {
	short length = a[0];
	short i;
	
	if (length != b[0])
		return false;
	
	for (i = 1; i <= length; i++)
		if (a[i] != b[i])
			return false;
	
	return true;
}

/* DialogItemTitleIs()
   True if dialog item itemNumber's own title text matches
   expectedTitle exactly. Assumes itemNumber is a button/control item -
   only ever called here with kFirstButtonItem/kSecondButtonItem, the
   DITL's two actual buttons (see the resource requirement documented
   at the top of this file). */
static Boolean DialogItemTitleIs(DialogPtr dialog, short itemNumber, const unsigned char *expectedTitle) {
	short	itemType;
	Handle	itemHandle;
	Rect	itemRect;
	Str255	itemTitle;
	
	GetDItem(dialog, itemNumber, &itemType, &itemHandle, &itemRect);
	GetCTitle((ControlHandle) itemHandle, itemTitle);
	
	return PascalStringsEqual(itemTitle, expectedTitle);
}

/* CenterDialogOverMainWindow()
   Repositions dialog so its centre lands on mwWindow's own centre, in
   global coordinates - called before the dialog is ever shown, so
   there's no visible jump from wherever its DLOG resource happened to
   place it. ResEdit has no way to express "centred over a particular
   window" in a resource - a DLOG's bounds are a fixed, absolute
   screen position - so this has to happen in code, especially given
   mwWindow itself can be dragged and resized (via its grow box - see
   TrackWindowResize()), so any position baked into the resource would
   only be centred by coincidence, and only until the window moved.
   
   Clamps the result to stay fully on screen, with a small margin in
   from each edge, in case mwWindow is positioned close enough to an
   edge that a naive centring would push the dialog partially off it. */
#define kScreenEdgeMargin	4

/* GetScreenBoundsForWindow()
   Finds the bounds, in global coordinates, of whichever screen
   mwWindow is actually on - not always the main screen, on a
   multiple-monitor system.
   
   Falls back to screenBits.bounds (the only screen that could
   possibly exist) when gHasColorQD is false: multiple screens require
   Color QuickDraw's Device Manager extensions in the first place,
   true of every real Mac Plus, this project's stated minimum target.
   Also falls back there if GetMaxDevice() can't find a screen the
   window intersects at all (NULL) - an edge case that shouldn't come
   up in practice, but cheap insurance against ever centring on a
   garbage rect.
   
   GetMaxDevice() is documented as finding the device with the
   greatest pixel depth among those intersecting a given rect, not
   specifically "the one most of a window is on" - but it's the long-
   established idiom for exactly this question regardless (see, for
   instance, MacTech's "Multiple Monitors vs. Your Application"),
   since in practice a window only spans more than one screen right at
   the boundary between them, where any reasonable choice of "which
   screen" is equally fine. Real C usage of this call, in that same
   published example, takes globalRect by pointer despite Inside
   Macintosh's Pascal signature showing it by value - matched here. */
static void GetScreenBoundsForWindow(Rect *outBounds) {
	GrafPtr		savedPort;
	Rect		windowGlobalRect;
	GDHandle	device;
	
	if (!gHasColorQD) {
		*outBounds = screenBits.bounds;
		return;
	}
	
	GetPort(&savedPort);
	SetPort(mwWindow);
	windowGlobalRect = ((GrafPtr) mwWindow)->portRect;
	LocalToGlobal((Point *) &windowGlobalRect);
	LocalToGlobal(1 + (Point *) &windowGlobalRect);
	SetPort(savedPort);
	
	device = GetMaxDevice(&windowGlobalRect);
	
	*outBounds = device ? (**device).gdRect : screenBits.bounds;
}

/* CenterDialogOverMainWindow()
   Repositions dialog so its centre lands on mwWindow's own centre, in
   global coordinates - called before the dialog is ever shown, so
   there's no visible jump from wherever its DLOG resource happened to
   place it. ResEdit has no way to express "centred over a particular
   window" in a resource - a DLOG's bounds are a fixed, absolute
   screen position - so this has to happen in code, especially given
   mwWindow itself can be dragged and resized (via its grow box - see
   TrackWindowResize()), so any position baked into the resource would
   only be centred by coincidence, and only until the window moved.
   
   Falls back to centring on the whole screen mwWindow is on (see
   GetScreenBoundsForWindow() - not necessarily the main screen, on a
   multiple-monitor system) whenever centring on the window itself
   wouldn't work cleanly: either the dialog is larger than the window,
   so it wouldn't actually fit inside it, or the window is close
   enough to a screen edge that window-centring would push the dialog
   partially off screen. Falling back to a full re-centre, rather than
   nudging the window-centred position back onto the screen, avoids
   the dialog ending up pinned against one edge - centred on
   something, rather than arbitrarily placed. */
static void CenterDialogOverMainWindow(DialogPtr dialog) {
	GrafPtr	savedPort;
	Point	windowTopLeft;
	short	windowWidth, windowHeight;
	short	dialogWidth, dialogHeight;
	short	newLeft, newTop;
	Rect	screenBounds;
	Boolean	fitsInsideWindow;
	Boolean	fitsOnScreen;
	
	GetPort(&savedPort);
	SetPort(mwWindow);
	windowTopLeft.h = 0;
	windowTopLeft.v = 0;
	LocalToGlobal(&windowTopLeft);
	SetPort(savedPort);
	
	GetFractalResolution(&windowWidth, &windowHeight);
	
	dialogWidth  = ((GrafPtr) dialog)->portRect.right  - ((GrafPtr) dialog)->portRect.left;
	dialogHeight = ((GrafPtr) dialog)->portRect.bottom - ((GrafPtr) dialog)->portRect.top;
	
	GetScreenBoundsForWindow(&screenBounds);
	
	fitsInsideWindow = (dialogWidth <= windowWidth) && (dialogHeight <= windowHeight);
	
	newLeft = windowTopLeft.h + (windowWidth  - dialogWidth)  / 2;
	newTop  = windowTopLeft.v + (windowHeight - dialogHeight) / 2;
	
	fitsOnScreen = (newLeft >= screenBounds.left + kScreenEdgeMargin)
			&& (newTop  >= screenBounds.top  + kScreenEdgeMargin)
			&& (newLeft + dialogWidth  <= screenBounds.right  - kScreenEdgeMargin)
			&& (newTop  + dialogHeight <= screenBounds.bottom - kScreenEdgeMargin);
	
	if (!fitsInsideWindow || !fitsOnScreen) {
		newLeft = screenBounds.left + ((screenBounds.right  - screenBounds.left) - dialogWidth)  / 2;
		newTop  = screenBounds.top  + ((screenBounds.bottom - screenBounds.top)  - dialogHeight) / 2;
	}
	
	MoveWindow(dialog, newLeft, newTop, false);
}

/* ShowConfirmationDialog()
   Shows the DLOG/DITL resource at dialogID and blocks until one of
   its two buttons is chosen, returning true if the clicked button's
   own title matches affirmativeTitle exactly. Shared by ConfirmZoom()
   and ConfirmUseLargestSize() - both dialogs have the same shape (one
   line of text, then exactly two buttons), just at different resource
   IDs with different affirmative button text - see the resource
   requirement documented at the top of this file.
   
   Checks for the DLOG resource explicitly, via GetResource()/ResError(),
   before ever calling GetNewDialog() - rather than relying solely on
   GetNewDialog() itself to fail gracefully when the resource is
   missing. Also centres the dialog over mwWindow (CenterDialogOverMainWindow())
   and forces it on screen via ShowWindow()/SelectWindow() regardless
   of the resource's own "initially visible" flag. The last two came
   from real testing: mwWindow can be dragged and resized, so a fixed
   position baked into the DLOG resource would only be centred by
   coincidence and only until the window moved - ResEdit has no way to
   express "centred over a particular window" in a resource, so this
   has to happen in code; and the visibility flag was found unchecked
   on the first dialog built, leaving GetNewDialog() creating the
   window but never showing it. SysBeep() makes a missing/malformed
   resource audible immediately rather than invisible - the leading
   suspect for any failure here, being the very first time this
   project used the Dialog Manager at all; it and a NULL GetNewDialog()
   both return false, declining whatever the dialog was confirming,
   rather than crashing or guessing.
   
   Determines which item number is affirmativeTitle once, via
   DialogItemTitleIs(), right after the dialog is created and before
   either button has been clicked - rather than reading the clicked
   item's own title back out after ModalDialog() returns, which an
   earlier version of ConfirmZoom() did. That version fixed a click on
   "Zoom" not registering (the DITL's two buttons had ended up
   numbered the other way around from a fixed assumption of which was
   which), but real testing then found "Cancel" stopped working -
   consistent with something about reading a control's title back out
   right after tracking a click on it, specifically, not behaving the
   same way for both buttons. Reading both titles once, before any
   interaction, removes that timing question entirely: itemHit only
   ever needs comparing against a plain item number from that point
   on. */
static Boolean ShowConfirmationDialog(short dialogID, const unsigned char *affirmativeTitle) {
	DialogPtr	dialog;
	short		itemHit;
	short		affirmativeItemNumber;
	Boolean		confirmed;
	Handle		dlogResource;
	
	dlogResource = GetResource('DLOG', dialogID);
	if (dlogResource == NULL || ResError() != noErr) {
		SysBeep(10);
		return false;
	}
	
	dialog = GetNewDialog(dialogID, NULL, (WindowPtr) -1L);
	if (dialog == NULL) {
		SysBeep(10);
		return false;
	}
	
	/* Forces the dialog invisible before repositioning it, regardless
	   of the DLOG resource's own "initially visible" flag - the first
	   dialog built had that flag unchecked (see CenterDialogOverMainWindow()'s
	   caller below forcing it back on), but real testing found a
	   white hole punched in mwWindow's own top-left corner, matching
	   the dialog's size, that persisted until the dialog closed -
	   consistent with the dialog briefly existing, visible, at its
	   original resource position (close to mwWindow's own top-left)
	   before CenterDialogOverMainWindow() moves it, and mwWindow's
	   own update for the area that briefly covered never getting
	   processed while ModalDialog()'s own event loop has control.
	   Hiding first, moving, then showing again makes this safe
	   regardless of what either DLOG resource's flag actually says. */
	HideWindow(dialog);
	CenterDialogOverMainWindow(dialog);
	
	ShowWindow(dialog);
	SelectWindow(dialog);
	
	affirmativeItemNumber = DialogItemTitleIs(dialog, kFirstButtonItem, affirmativeTitle)
			? kFirstButtonItem : kSecondButtonItem;
	
	do {
		ModalDialog(NULL, &itemHit);
	} while (itemHit != kFirstButtonItem && itemHit != kSecondButtonItem);
	
	confirmed = (itemHit == affirmativeItemNumber);
	
	DisposeDialog(dialog);
	
	return confirmed;
}

/* ConfirmZoom()
   See mwZoom.h's resource requirement (Zoom Confirmation, ID 128). */
static Boolean ConfirmZoom(void) {
	return ShowConfirmationDialog(kZoomConfirmDialogID, (const unsigned char *) kZoomButtonTitle);
}

/* ConfirmUseLargestSize()
   Shown by TrackWindowResize() when the requested size looks too
   large to allocate (see the resource requirement documented at the
   top of this file - Insufficient Memory, ID 129). True if the person
   chooses to proceed with the largest size that does fit instead of
   cancelling the resize outright. */
static Boolean ConfirmUseLargestSize(void) {
	return ShowConfirmationDialog(kInsufficientMemoryDialogID, (const unsigned char *) kOKButtonTitle);
}

/* FindLargestSizeFittingMemory()
   Shrinks maxWidth/maxHeight by 10% at a time - simple and safe from
   rounding surprises, rather than solving the (roughly quadratic)
   width-to-memory relationship directly - until
   EstimateOffscreenBytesNeeded() (mwWindow.h) says the result fits
   within availableBytes, or until hitting kMinimumWindowWidth/Height.
   Re-snaps the result to an exact kAspectRatioNumerator:Denominator
   ratio at the end via ConstrainToAspectRatio(), clearing out
   whatever small drift the repeated 9/10 shrinking's integer rounding
   may have introduced. */
static void FindLargestSizeFittingMemory(short maxWidth, short maxHeight, long availableBytes, short *outWidth, short *outHeight) {
	short	width  = maxWidth;
	short	height = maxHeight;
	Point	zero, corner;
	Rect	snapped;
	
	while (width > kMinimumWindowWidth &&
			EstimateOffscreenBytesNeeded(width, height) > availableBytes) {
		width  = (short) (((long) width  * 9) / 10);
		height = (short) (((long) height * 9) / 10);
	}
	
	if (width < kMinimumWindowWidth) {
		width  = kMinimumWindowWidth;
		height = kMinimumWindowHeight;
	}
	
	zero.h = 0;
	zero.v = 0;
	corner.h = width;
	corner.v = height;
	ConstrainToAspectRatio(&snapped, zero, corner, kAspectRatioNumerator, kAspectRatioDenominator);
	
	*outWidth  = snapped.right  - snapped.left;
	*outHeight = snapped.bottom - snapped.top;
}

/* TrackWindowResize()
   See mwZoom.h. Tracks mwWindow's grow box: rather than calling
   GrowWindow() directly, which has no notion of a locked aspect
   ratio, this runs its own tracking loop - structurally the same as
   TrackMarqueeAndZoom()'s, and reusing the same ConstrainToAspectRatio()
   helper - XOR-outlining the proposed new window frame as the mouse
   moves, anchored at mwWindow's own local (0,0) origin (its top-left
   corner, which never moves during a resize; only the bottom-right,
   where the grow box lives, does).
   
   Draws entirely within mwWindow's own port, in its own local
   coordinates - not the Window Manager port, which an earlier version
   of this function switched to (via GetWMgrPort()/GetCWMgrPort()) so
   the outline could extend past mwWindow's current bounds without
   being clipped. Real testing crashed immediately on entering this
   function, right after confirming (via a temporary diagnostic) that
   FindWindow() correctly recognises the click as inGrow in the first
   place - narrowing the fault to something inside here, and
   GetWMgrPort()/GetCWMgrPort() were the one thing in this function
   with no precedent anywhere else in this project (everything else -
   SetPort(), FrameRect(), PenMode(), GetMouse() - is already proven
   working via TrackMarqueeAndZoom()). Given this project's history of
   exactly this pattern (CTabChanged(), PmForeColor() - see
   mwColorCycle.c/mwWindow.c), removing the untested call outright
   seemed a safer fix than chasing down why it crashed.
   
   The real cost of dropping the Window Manager port: the outline is
   now clipped to mwWindow's current bounds like any other drawing
   into it, so growing the window larger than its current size shows
   only the portion of the outline that still fits within the old
   bounds while dragging - a real, visible limitation, but a
   functioning one. Shrinking the window is unaffected, since the
   whole proposed frame is always within the current, larger bounds
   in that direction. The final resize itself (SizeWindow(),
   HandleWindowResized()) doesn't depend on what was drawn regardless,
   so this only affects the live preview, not the result.
   
   Clamps the proposed size to kMinimumWindowWidth/Height at the small
   end, and to the largest kAspectRatioNumerator:Denominator rectangle
   that still fits between the window's current position and the
   screen's own edges at the large end - both computed once, before
   the tracking loop starts, since neither the window's position nor
   the screen's size can change during the drag. */
void TrackWindowResize(Point globalMouseDownPoint) {
	GrafPtr	savedPort;
	Point	origin;
	Point	currentPoint;
	Point	screenBottomRightLocal;
	Rect	proposedFrame, previousFrame, maxFrame;
	Boolean	haveDrawnAFrame = false;
	short	maxWidth, maxHeight;
	
	GetPort(&savedPort);
	SetPort(mwWindow);
	
	origin.h = 0;
	origin.v = 0;
	
	screenBottomRightLocal.h = screenBits.bounds.right;
	screenBottomRightLocal.v = screenBits.bounds.bottom;
	GlobalToLocal(&screenBottomRightLocal);
	
	ConstrainToAspectRatio(&maxFrame, origin, screenBottomRightLocal, kAspectRatioNumerator, kAspectRatioDenominator);
	maxWidth  = maxFrame.right  - maxFrame.left;
	maxHeight = maxFrame.bottom - maxFrame.top;
	
	PenMode(patXor);
	
	while (StillDown()) {
		short width, height;
		
		GetMouse(&currentPoint);
		
		ConstrainToAspectRatio(&proposedFrame, origin, currentPoint, kAspectRatioNumerator, kAspectRatioDenominator);
		
		width  = proposedFrame.right  - proposedFrame.left;
		height = proposedFrame.bottom - proposedFrame.top;
		
		if (width < kMinimumWindowWidth || height < kMinimumWindowHeight) {
			width  = kMinimumWindowWidth;
			height = kMinimumWindowHeight;
		} else if (width > maxWidth || height > maxHeight) {
			width  = maxWidth;
			height = maxHeight;
		}
		
		SetRect(&proposedFrame, 0, 0, width, height);
		
		if (!haveDrawnAFrame || !EqualRect(&proposedFrame, &previousFrame)) {
			if (haveDrawnAFrame)
				FrameRect(&previousFrame);	/* erase the previous frame */
			
			FrameRect(&proposedFrame);
			previousFrame   = proposedFrame;
			haveDrawnAFrame = true;
		}
	}
	
	if (haveDrawnAFrame)
		FrameRect(&previousFrame);	/* erase the final frame */
	
	PenMode(patCopy);
	SetPort(savedPort);
	
	if (!haveDrawnAFrame)
		return;
	
	{
		short newWidth  = previousFrame.right  - previousFrame.left;
		short newHeight = previousFrame.bottom - previousFrame.top;
		short currentWidth, currentHeight;
		long  neededBytes, grow, availableBytes;
		
		GetFractalResolution(&currentWidth, &currentHeight);
		
		if (newWidth == currentWidth && newHeight == currentHeight)
			return;
		
		/* Checks whether the requested size looks likely to fail to
		   allocate before ever attempting the resize, rather than
		   discovering that partway through HandleWindowResized() (by
		   which point DisposeOffscreenStore() has already freed the
		   old, working offscreen store - real testing crashed here,
		   at a size too large for available memory, before this
		   check existed). MaxMem() actively compacts and purges the
		   heap and returns the actual largest contiguous block
		   achievable, rather than FreeMem()'s total free space, which
		   Inside Macintosh itself notes usually can't be allocated as
		   one block due to fragmentation - MaxMem() is the more
		   reliable answer to "would this specific allocation actually
		   succeed right now". */
		neededBytes    = EstimateOffscreenBytesNeeded(newWidth, newHeight);
		availableBytes = MaxMem(&grow);
		
		if (neededBytes > availableBytes) {
			if (!ConfirmUseLargestSize())
				return;
			
			FindLargestSizeFittingMemory(maxWidth, maxHeight, availableBytes, &newWidth, &newHeight);
			
			if (newWidth == currentWidth && newHeight == currentHeight)
				return;
		}
		
		SizeWindow(mwWindow, newWidth, newHeight, true);
		HandleWindowResized(newWidth, newHeight);
	}
}

/* KeyboardZoom()
   See mwZoom.h. '-' doubles gView.halfWidthRe (zoom out to double the
   visible area, centre unchanged); '+' halves it (zoom into the
   centre half of the current view on each axis - the span shrinks,
   the centre doesn't move). ClampHalfWidthRe() (mwWindow.h) keeps
   repeated presses from zooming in past the point float precision in
   the per-pixel iteration can resolve, or out past the current
   fractal's own natural extent. */
void KeyboardZoom(Boolean zoomIn) {
	double proposedHalfWidthRe;
	
	if (!IsZoomOutAvailable() || IsRenderActive())
		return;
	
	proposedHalfWidthRe = zoomIn ? (gView.halfWidthRe / 2.0) : (gView.halfWidthRe * 2.0);
	gView.halfWidthRe    = ClampHalfWidthRe(proposedHalfWidthRe);
	
	RenderFractalOffscreen();
	DrawContent(((WindowPeek) mwWindow)->hilited);
}
