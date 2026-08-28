/*****
 * mwZoom.h
 *
 *		Public interface for mwZoom.c - marquee selection and the
 *		zoom-to-selection feature.
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
