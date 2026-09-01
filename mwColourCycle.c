/*****
 * mwColourCycle.c
 *
 *		The Fractal menu's Animate/Stop Animation feature.
 *
 *		Classic Mac colour-cycling works by rotating a device's colour
 *		table while the underlying pixel data - palette indices - never
 *		changes, which is why it can run smoothly even on very old
 *		hardware: nothing gets redrawn, just reinterpreted. This file
 *		does exactly that with the fractal's own offscreen colour
 *		table (see mwWindow.h's GetOffscreenColourTable()) rather than
 *		anything screen-wide, so it only ever affects this one window.
 *
 *		On black-and-white Macs there's no colour table to rotate, so
 *		"animate" instead means periodically redrawing the same
 *		already-known shade levels through a shifted pattern mapping
 *		(see mwWindow.h's ApplyMonoPatternPhase()) - still a redraw,
 *		since there's no indirection to exploit the way there is with
 *		an indexed colour table, but a cheap one: pattern lookups and
 *		FillRect calls, never the fractal maths itself.
 *
 *		Both paths are driven by the same tick-gated check from
 *		HandleEvent()'s idle branch (AnimationTask()) rather than a
 *		Time Manager task - this project already relies on exactly
 *		this pattern for RefreshInfoWindowIfNeeded() (see mwInfo.c),
 *		and introducing Time Manager's own set of constraints for a
 *		once-every-couple-of-ticks check isn't worth what it would add.
 *
 *****/
#include "mwColourCycle.h"
#include "mwWindow.h"
#ifndef _Quickdraw_
#include <Quickdraw.h>
#endif

/* Roughly every 1-3 ticks, per the original request. Both the colour
   and mono paths share one cadence - there's no reason for one to
   animate faster than the other, and a single constant is one less
   thing to keep in sync if this ever changes. */
#define kAnimationIntervalTicks	2

static Boolean			gAnimating = false;
static unsigned long	gLastAnimationTick = 0;

/* Persists across stop/start on purpose: stopping animation should
   leave the image exactly as it currently looks, not snap back to
   the un-rotated starting colours or the un-shifted pattern mapping -
   so nothing here ever resets this back to 0. */
static short			gPatternPhase = 0;

/* Which way continuous animation currently runs, and which way a
   single arrow-key step goes while it's stopped - see
   AnimationArrowKeyPressed(). Defaults to forward, matching the
   direction animation always ran before this existed. */
static Boolean			gAnimationDirectionForward = true;

static void RotateColourTable(CTabHandle table, Boolean forward);
static void AdvanceOneFrame(Boolean forward);

void ToggleAnimation(void) {
	gAnimating = !gAnimating;
	gLastAnimationTick = TickCount();
}

Boolean IsAnimationActive(void) {
	return gAnimating;
}

/* AnimationTask()
   See mwColourCycle.h. Declines to do anything - without disturbing
   gLastAnimationTick, so the next eligible tick still fires on
   schedule - while a render is in progress: mid-render, the offscreen
   image (and, for mono, the recorded shade levels behind
   ApplyMonoPatternPhase()) is a moving target, and animating it would
   just look like noise rather than the intended effect. Animation
   picks back up automatically once the render completes, since
   nothing here needs to know that happened - the same tick check
   simply stops bailing out on this line. */
void AnimationTask(void) {
	unsigned long now;
	
	if (!gAnimating)
		return;
	
	now = TickCount();
	if (now - gLastAnimationTick < kAnimationIntervalTicks)
		return;
	
	if (IsRenderActive())
		return;
	
	gLastAnimationTick = now;
	
	AdvanceOneFrame(gAnimationDirectionForward);
}

/* AnimationArrowKeyPressed()
   See mwColourCycle.h. forward is true for up/right, false for
   down/left (MandyWindow.c's HandleEvent() maps the actual key codes).
   
   Always records the new direction, even while animation is running
   and stopped, so continuous animation (AnimationTask()) picks it up
   on its very next tick regardless of which state it's in when the
   key is pressed.
   
   If animation is already running, that's all this does - the
   direction change is enough, and AdvanceOneFrame() shouldn't fire
   twice in the same brief window (once here, once from the next
   scheduled tick). If animation is stopped, this instead steps
   exactly one frame immediately, so arrow keys work as a manual
   frame-by-frame control without needing continuous animation
   running at all. */
void AnimationArrowKeyPressed(Boolean forward) {
	gAnimationDirectionForward = forward;
	
	if (gAnimating)
		return;
	
	if (IsRenderActive() || !IsAnimationAvailable())
		return;
	
	AdvanceOneFrame(forward);
}

/* AdvanceOneFrame()
   Rotates the colour table, or steps the mono pattern phase, by
   exactly one frame in the given direction, then shows the result.
   Shared by the continuous animation tick (AnimationTask()) and the
   single-step arrow-key handler (AnimationArrowKeyPressed()) - both
   do exactly this, just on different triggers. */
static void AdvanceOneFrame(Boolean forward) {
	if (IsRenderingInColour()) {
		CTabHandle table = GetOffscreenColourTable();
		
		if (table == NULL)
			return;
		
		RotateColourTable(table, forward);
	} else {
		gPatternPhase += forward ? 1 : -1;
		ApplyMonoPatternPhase(gPatternPhase);
	}
	
	RefreshWholeDisplay();
}

/* RotateColourTable()
   Shifts every entry's RGB by one slot - forward wraps the last entry
   around to become the first; backward is the exact reverse, wrapping
   the first entry around to become the last - a standard colour-
   cycling rotation, run either direction. Directly mutates the
   offscreen GWorld's own colour table (see GetOffscreenColourTable())
   rather than going through SetGWorld() or any Palette Manager call:
   SetGWorld() is confirmed, from earlier testing on this project, to
   crash on real hardware, and the Palette Manager routines this
   feature was originally specified with (SetPalette(), PmForeColor()'s
   family) aren't linked into this project and pulling them in wasn't
   chased down (see ShadeBlock() in mwWindow.c for the same conclusion
   reached the same way). Directly mutating the CTabHandle's own
   entries needs neither - the next CopyBits() (via
   RefreshWholeDisplay()) reads whatever this table currently holds
   regardless of which device or port is current.
   
   Reads the entry count from the table itself (ctSize is count-1)
   rather than assuming a fixed range, so this stays correct if the
   fractal colour ramp's own entry count (kShadingScale+1, in
   mwWindow.c) ever changes. */
static void RotateColourTable(CTabHandle table, Boolean forward) {
	short		entryCount = GetRotatableColourTableEntryCount();
	RGBColor	wrapped;
	short		i;
	
	if (entryCount < 2)
		return;
	
	if (forward) {
		wrapped = (**table).ctTable[entryCount - 1].rgb;
		
		for (i = entryCount - 1; i > 0; i--)
			(**table).ctTable[i].rgb = (**table).ctTable[i - 1].rgb;
		
		(**table).ctTable[0].rgb = wrapped;
	} else {
		wrapped = (**table).ctTable[0].rgb;
		
		for (i = 0; i < entryCount - 1; i++)
			(**table).ctTable[i].rgb = (**table).ctTable[i + 1].rgb;
		
		(**table).ctTable[entryCount - 1].rgb = wrapped;
	}
	
	/* Tells QuickDraw this table's contents changed by giving it a
	   fresh seed value - it can cache colour-matching results keyed
	   by ctSeed, and without this, those caches could keep showing
	   the colours the table held before the rotation. CTabChanged()
	   is Color QuickDraw's documented routine for exactly this
	   situation, but it isn't linked into this project (confirmed by
	   an "undefined" error on real testing) - the same kind of gap
	   PmForeColor() hit earlier (see ShadeBlock() in mwWindow.c).
	   GetCTSeed() is a much more basic call already proven to link
	   here: BuildFractalColourTable() already uses it, the same way,
	   to set this table's seed when it's first built. */
	(**table).ctSeed = GetCTSeed();
}
