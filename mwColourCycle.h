/*****
 * mwColourCycle.h
 *
 *		Public interface for mwColourCycle.c - the Fractal menu's
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
   rotation (see mwWindow.h's GetOffscreenColourTable()) or the mono
   pattern-phase redraw (see mwWindow.h's ApplyMonoPatternPhase()),
   according to mwWindow.h's IsRenderingInColour() - the person doesn't
   choose between them, the same "Animate" item does the right thing
   on whatever hardware this happens to be running on. Always runs in
   whichever direction was last set by AnimationArrowKeyPressed(). */
void AnimationTask(void);

/* Called on an up/right or down/left arrow key press - forward is
   true for up/right, false for down/left. Sets the direction
   continuous animation runs in from here on, whether or not animation
   is currently running. If animation is stopped, this additionally
   steps the colour table or pattern phase by exactly one frame in the
   given direction right away, so the arrow keys double as a manual,
   one-frame-at-a-time control when Animate isn't turned on. */
void AnimationArrowKeyPressed(Boolean forward);
