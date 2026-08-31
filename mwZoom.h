/*****
 * mwZoom.h
 *
 *		Public interface for mwZoom.c - marquee selection, resizing
 *		the window via its grow box, and keyboard zoom: everything
 *		that changes the fractal's visible region or the window's
 *		own shape.
 *
 *****/

/* Tracks a rubber-band marquee selection starting from
   globalMouseDownPoint (in global coordinates, exactly as delivered
   in an EventRecord's where field - this converts to mwWindow's own
   local coordinates itself), constrained to a fixed aspect ratio as
   it's dragged (currently 5:3 - see kAspectRatioNumerator/Denominator
   in mwZoom.c). Blocks until the mouse is released, matching how
   classic Mac drag-tracking loops normally work.
   
   If the resulting marquee is a meaningful size, asks for
   confirmation - via the "Zoom Confirmation" DLOG/DITL resource pair
   this needs at ID 128, described at the top of mwZoom.c - before
   zooming gView to it and re-rendering. Too small a marquee (an
   accidental click) is treated as no selection at all, with nothing
   asked and nothing changed.
   
   Called from MandyWindow.c's HandleMouseDown() when a mouse-down
   lands in mwWindow's content while it's already the front window. */
void TrackMarqueeAndZoom(Point globalMouseDownPoint);

/* Tracks a resize of mwWindow's own grow box, starting from
   globalMouseDownPoint - the same global-coordinate convention as
   TrackMarqueeAndZoom() - constrained to the same fixed aspect ratio
   as the marquee. Blocks until the mouse is released; on release, if
   the size actually changed, resizes mwWindow and reallocates the
   offscreen store to match (see mwWindow.h's HandleWindowResized()).
   
   Called from MandyWindow.c's HandleMouseDown() on an inGrow click in
   mwWindow. */
void TrackWindowResize(Point globalMouseDownPoint);

/* Called on a '+' (zoom in, halving the visible span on each axis,
   centred on the current view's own centre) or '-' (zoom out,
   doubling it) key press - see MandyWindow.c's HandleEvent(). Does
   nothing for the Tree (no zoomable view to adjust), or while a
   render is in progress. */
void KeyboardZoom(Boolean zoomIn);
