/*****
 * mwColorCycle.h
 *
 *		Public interface for mwColorCycle.c - the Fractal menu's
 *		Animate/Stop Animation feature.
 *
 *****/

/* Starts animating if it's currently stopped, or stops it if it's
   currently running - called from mwMenus.c's HandleMenu() when the
   Animate/Stop Animation item is chosen. Stopping leaves whatever
   colours or patterns are currently showing exactly as they are -
   nothing here ever resets the offscreen colour table or the mono
   pattern phase back to their starting state. */
void ToggleAnimation(void);

/* True while animation is running - mwMenus.c uses this to decide
   whether the menu item should currently read "Animate" or
   "Stop Animation". */
Boolean IsAnimationActive(void);

/* Call once per HandleEvent() idle cycle, alongside AdvanceFractalRender()
   and RefreshInfoWindowIfNeeded(). Does nothing at all, cheaply, unless
   animation is currently running. Internally picks real colour-table
   rotation (see mwWindow.h's GetOffscreenColorTable()) or the mono
   pattern-phase redraw (see mwWindow.h's ApplyMonoPatternPhase()),
   according to mwWindow.h's IsRenderingInColor() - the person doesn't
   choose between them, the same "Animate" item does the right thing
   on whatever hardware this happens to be running on. */
void AnimationTask(void);
