/*****
 * mwZoom.c
 *
 *		Marquee selection on the fractal window, and the zoom-to-
 *		selection feature it drives.
 *
 *		REQUIRES a "Zoom Confirmation" DLOG/DITL resource pair at
 *		ID 128 (kZoomConfirmDialogID below) - a resource, not
 *		something this file can create. As actually built:
 *
 *		  DITL 128, three items in this order:
 *		    1. StaticText, "Zoom to the selected area?"
 *		    2. Button,     "Zoom"
 *		    3. Button,     "Cancel"
 *
 *		  (This is a different order than originally specified -
 *		  Zoom/Cancel/text - which put the two buttons first; building
 *		  the text label first, as ResEdit naturally encourages, is
 *		  just as fine, since ConfirmZoom() finds "Zoom" by its actual
 *		  title text and item number 1's own text is never inspected -
 *		  only its position matters, as the one item not itself a
 *		  button. If this DITL is ever rebuilt with a different number
 *		  of items, or the text moved to a different slot, kTextItem/
 *		  kFirstButtonItem/kSecondButtonItem below need to move with
 *		  it.)
 *
 *		  DLOG 128, referencing DITL 128, procID dBoxProc (a plain box
 *		  with no title bar - not a document window, and not one of
 *		  ResEdit's numbered/custom WDEF slots), goAway off (it's
 *		  dismissed by its own buttons, not a close box). The DLOG's
 *		  own "initially visible" flag doesn't matter either way -
 *		  ConfirmZoom() calls ShowWindow() itself rather than relying
 *		  on it, after real testing found that flag unchecked and the
 *		  dialog consequently never appearing at all.
 *
 *****/
#include "mwZoom.h"
#include "mwWindow.h"
#ifndef _Quickdraw_
#include <Quickdraw.h>
#endif
#ifndef _Dialogs_
#include <Dialogs.h>
#endif

extern	WindowPtr	mwWindow;

/* Below this, in either dimension, a marquee is treated as an
   accidental click-drag rather than a deliberate selection - avoids
   popping the confirmation dialog for a one-or-two-pixel jiggle. */
#define kMinimumMarqueeSize		8

/* The marquee's aspect ratio. Kept as two separate numbers rather
   than a single named "5x3" constant so a future change to this
   ratio - already revised once, from an initial 4:3 - is a one-line
   edit here rather than a rename of ConstrainToAspectRatio() and
   every call to it. Doesn't need to match the window's own current
   512x300 shape exactly; see mwZoom.h and the project notes on the
   still-to-come resizable, aspect-locked window this is expected to
   eventually line up with. */
#define kAspectRatioNumerator	5
#define kAspectRatioDenominator	3

#define kZoomConfirmDialogID	128
#define kTextItem				1
#define kFirstButtonItem		2
#define kSecondButtonItem		3
#define kZoomButtonTitle		"\pZoom"

static void    ConstrainToAspectRatio(Rect *r, Point anchor, Point current, short ratioNumerator, short ratioDenominator);
static void    ClampPointToImageBounds(Point *p);
static Boolean PascalStringsEqual(const unsigned char *a, const unsigned char *b);
static Boolean DialogItemTitleIs(DialogPtr dialog, short itemNumber, const unsigned char *expectedTitle);
static Boolean ConfirmZoom(void);

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

/* ConfirmZoom()
   Shows the "Zoom Confirmation" dialog (see the resource requirement
   documented at the top of this file) and blocks until one of its two
   buttons is chosen.
   
   Checks for the DLOG resource explicitly, via GetResource()/ResError(),
   before ever calling GetNewDialog() - rather than relying solely on
   GetNewDialog() itself to fail gracefully when the resource is
   missing. This is the very first Dialog Manager code in this project,
   and the resource it depends on is brand new, so a missing or
   malformed DLOG/DITL is the leading suspect for any failure here;
   SysBeep() makes that failure audible immediately rather than
   invisible.
   
   Determines which item number is "Zoom" once, via DialogItemTitleIs(),
   right after the dialog is created and before either button has been
   clicked - rather than reading the clicked item's own title back out
   after ModalDialog() returns, which an earlier version of this
   function did. That version fixed a click on "Zoom" not registering
   (the DITL's two buttons had ended up numbered the other way around
   from a fixed assumption of which was which), but real testing then
   found "Cancel" stopped working - consistent with something about
   reading a control's title back out right after tracking a click on
   it, specifically, not behaving the same way for both buttons.
   Reading both titles once, before any interaction, removes that
   timing question entirely: itemHit only ever needs comparing against
   a plain item number from that point on. */
static Boolean ConfirmZoom(void) {
	DialogPtr	dialog;
	short		itemHit;
	short		zoomItemNumber;
	Boolean		confirmed;
	Handle		dlogResource;
	
	dlogResource = GetResource('DLOG', kZoomConfirmDialogID);
	if (dlogResource == NULL || ResError() != noErr) {
		SysBeep(10);
		return false;
	}
	
	dialog = GetNewDialog(kZoomConfirmDialogID, NULL, (WindowPtr) -1L);
	if (dialog == NULL) {
		SysBeep(10);
		return false;
	}
	
	/* Forces the dialog on screen regardless of the DLOG resource's
	   own "initially visible" flag - real testing found that flag
	   unchecked, which left GetNewDialog() creating the window but
	   never showing it, so ModalDialog() sat waiting for a click on
	   buttons nobody could see or reach. Not relying on getting that
	   checkbox right in the resource going forward. */
	ShowWindow(dialog);
	SelectWindow(dialog);
	
	zoomItemNumber = DialogItemTitleIs(dialog, kFirstButtonItem, (const unsigned char *) kZoomButtonTitle)
			? kFirstButtonItem : kSecondButtonItem;
	
	do {
		ModalDialog(NULL, &itemHit);
	} while (itemHit != kFirstButtonItem && itemHit != kSecondButtonItem);
	
	confirmed = (itemHit == zoomItemNumber);
	
	DisposeDialog(dialog);
	
	return confirmed;
}
