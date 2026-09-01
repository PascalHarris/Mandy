/*****
 * mwWindow.h
 *
 *		Public interfaces for mwWindow.c
 *
 *****/

void DrawContent (short active);
void SetUpWindow(void);
void RenderFractalOffscreen(void);
void AdvanceFractalRender(void);
void AbortFractalRender(void);

/* Updates this file's own size-tracking state (windowWidth/
   windowHeight, imageStart) to newWidth/newHeight and reallocates the
   offscreen store at the new size, then starts a fresh render - see
   mwWindow.c. Does NOT resize mwWindow itself; call SizeWindow() first
   (see mwZoom.c's TrackWindowResize()). newWidth/newHeight should
   already be within sensible bounds before calling this - it only
   guards against an outright degenerate (zero or negative) size, not
   against anything larger than what's reasonable for the screen. */
void HandleWindowResized(short newWidth, short newHeight);

/* The visible region of the complex plane Mandelbrot and Julia render
   against - centreRe/centreIm is the middle of the view, halfWidthRe
   is half its width (so the full visible Re range is centreRe ±
   halfWidthRe); the visible Im range follows from halfWidthRe scaled
   by the window's own aspect ratio, so the view is never distorted
   regardless of how far in gView is zoomed.
   
   double, not float: gView.centreRe/centreIm need to keep meaningful
   precision even once halfWidthRe has shrunk very small from repeated
   zooming, which float's ~7 significant digits can't hold onto for
   long. The per-pixel iteration loop (IterateEscapeTime(), in
   mwWindow.c) still narrows to float right before iterating, for
   performance - so this extends how far a zoom can go before that
   narrowing starts to show as visible banding in the finest detail,
   but doesn't remove the limit entirely. Going further than that
   would need computing iterations in double (or a perturbation-based
   approach for arbitrary depth), which is a substantially bigger
   change than parameterising the viewport. */
typedef struct {
	double	centreRe;
	double	centreIm;
	double	halfWidthRe;
} FractalView;

extern FractalView gView;

/* Resets gView to whichever fractal is currently selected (width,
   extern'd directly elsewhere - see mwMenus.c) own natural default
   view. Called when a different fractal is chosen from the menu, so
   switching fractals always starts from that fractal's own view
   rather than carrying over whatever zoom or pan the previous one was
   left at. Harmless to call for the Tree, which doesn't use gView at
   all - it just does nothing observable. */
void ResetViewForCurrentFractal(void);

/* Converts a pixel position into the point in the complex plane it
   corresponds to, according to gView. Shared by SampleMandelbrot()/
   SampleJulia() (mwWindow.c) and the marquee zoom feature (mwZoom.c),
   both of which need exactly this mapping - the fractal samplers to
   know what to iterate, the marquee to know which region of the
   complex plane a dragged selection rectangle corresponds to. */
void MapPixelToComplexPlane(short x, short y, double *outRe, double *outIm);

/* Clamps a proposed gView.halfWidthRe to a sensible range for the
   current fractal: a floor low enough to avoid float-precision
   collapse in IterateEscapeTime()'s per-pixel iteration (mwWindow.c),
   and a ceiling matching the current fractal's own default view, so
   repeated zooming out can't show an ever-larger, meaningless region.
   Used by both the marquee zoom feature's candidate view and keyboard
   zoom (+/-) - see mwZoom.c. */
double ClampHalfWidthRe(double proposedHalfWidthRe);

/* Read-only access to render state, for the Get Info window (mwInfo.c). */
Boolean IsRenderActive(void);
unsigned long RenderElapsedTicks(void);
ConstStr255Param CurrentFractalName(void);
short CurrentScreenDepth(void);
void GetFractalResolution(short *outWidth, short *outHeight);

/* A deliberate overestimate of how many bytes the offscreen store
   (and, for mono, the shade-level buffer alongside it) would need at
   a given size - used to decide whether a proposed window size is
   likely to fail to allocate before ever attempting it (see mwZoom.c's
   TrackWindowResize()), not an exact figure. */
long EstimateOffscreenBytesNeeded(short width, short height);

/* The mathematical constants behind whichever fractal is currently
   selected (width, extern'd directly elsewhere in this project - see
   mwMenus.c/mwInfo.c - decides which fields are meaningful):
   maxIterations and zoom apply to Mandelbrot and Julia both;
   constantRe/constantIm are Julia's fixed c and are meaningless for
   Mandelbrot (c varies per pixel there, so there's no single value to
   show) or the tree (not an escape-time fractal at all). Fields not
   meaningful for the current fractal are set to 0. */
typedef struct {
	double	zoom;
	short	maxIterations;
	double	constantRe;
	double	constantIm;
} FractalParameters;

FractalParameters GetFractalParameters(void);

/* Read-only access to the offscreen image, for Save As (mwSaveAs.c) as
   well as this file's own DrawContent()/AdvanceFractalRender(). Returns
   false (leaving *bits/*bounds untouched) if there's nothing to read -
   the offscreen store failed to allocate under low memory. */
Boolean GetOffscreenImage(BitMap **bits, Rect *bounds);

/* Support for the Animate feature (mwColorCycle.c) -------------------
   Animation rotates the already-rendered image's colours/patterns in
   place rather than recomputing the fractal, so it stays cheap enough
   to run every couple of ticks. These expose just enough of this
   file's internals for mwColorCycle.c to do that: */

/* The offscreen colour GWorld's own colour table, for rotating its RGB
   entries directly (bypassing SetGWorld/the Palette Manager - see
   mwColorCycle.c for why). NULL if there's no colour offscreen store
   (no colour QuickDraw, or allocation failed under low memory). */
CTabHandle GetOffscreenColorTable(void);

/* How many of the offscreen colour table's entries mwColorCycle.c's
   RotateColorTable() may rotate - see the function's own comment in
   mwWindow.c for why this is less than the table's own full size. */
short GetRotatableColorTableEntryCount(void);

/* True if the current screen depth and gHasColorQD together mean
   fractals are actually being rendered in colour right now - i.e.
   ShouldRenderInColor(), exposed for mwColorCycle.c to decide which
   of colour-cycling or pattern-cycling applies. */
Boolean IsRenderingInColor(void);

/* True if Animate would actually do something useful right now.
   Colour cycling only needs the offscreen colour table to exist, so
   this always follows IsRenderingInColor() there. Mono pattern-cycling
   additionally needs gMonoShadeLevels to have both allocated
   successfully (see AllocateOffscreenMonoStore() - it's larger than
   the mono bitmap itself, so it's the more likely of the two to fail
   under real memory pressure) and to actually be populated - which,
   uniquely among the fractals, the Tree never does: it draws branches
   directly with MoveTo()/Line() rather than through ShadeBlock(), so
   there's no graduated shading to record or animate at all. */
Boolean IsAnimationAvailable(void);

/* Blits the whole current offscreen image to the window, exactly as
   an update event would - for mwColorCycle.c to call after rotating
   colours or patterns, without going through an actual update event. */
void RefreshWholeDisplay(void);

/* Redraws every finest-size cell of the mono offscreen image using
   its already-known shade level (recorded by ShadeBlock() during
   normal rendering - see RecordMonoShadeLevels() in mwWindow.c) and
   the given phase, then leaves the result in the offscreen store for
   the caller to blit via RefreshWholeDisplay(). Coalesces runs of
   same-band cells into single fills rather than one call per cell -
   still a real redraw (there's no indirection to exploit the way a
   rotated colour table gives colour animation), but the cheapest one
   available: pattern lookups and FillRect calls, never the fractal
   maths itself. Does nothing if the shade-level buffer doesn't exist
   (the offscreen store failed to allocate under low memory) or if a
   render is currently in progress (the recorded shade levels would be
   a mix of old and not-yet-updated values mid-render). */
void ApplyMonoPatternPhase(short phase);

/* Shows and selects mwWindow if it's currently hidden (the person
   closed it via its close box, which only hides it - see
   HandleEvent()'s inGoAway case - it's created once at launch and
   never disposed). Called before rendering a newly-selected fractal,
   so picking a fractal from the menu always has somewhere to show it.
   Does nothing if the window is already visible. */
void EnsureWindowVisible(void);

/* Support for the Zoom Out menu item (mwMenus.c) ---------------------
   Restores gView to the current fractal's own default view - reusing
   an instant, cached copy of that fractal's already-rendered default
   image when one is available, rather than always re-rendering from
   scratch. */

/* True while the current fractal (width) actually has a zoomable view
   to reset - Mandelbrot or Julia, not the Tree. mwMenus.c uses this to
   grey out Zoom Out rather than have it do nothing when clicked. */
Boolean IsZoomOutAvailable(void);

/* Support for the Palette submenu (mwMenus.c) ------------------------
   A palette only changes what colours the offscreen store's existing
   shade-level indices map to - never the indices themselves - so
   switching palettes is always instant, with no re-render, the same
   way one step of Animate's colour-table rotation is. */

/* How many palettes exist - GetCurrentPalette()/SetCurrentPalette()
   work in terms of a 0-based index below this. */
short GetPaletteCount(void);

/* The currently active palette's 0-based index. */
short GetCurrentPalette(void);

/* True while palette selection means anything at all - false on a
   black-and-white Mac, where there's no colour table for a palette to
   describe. mwMenus.c uses this to grey out (or omit) the Palette
   submenu accordingly. */
Boolean IsPaletteAvailable(void);

/* Selects a new palette (a no-op if paletteIndex is out of range) and
   immediately recolours the current image with it - rebuilds the
   offscreen colour table's RGB entries for paletteIndex, then blits
   again. Safe to call at any time, including mid-render, since it
   never touches the offscreen store's pixel data. */
void SetCurrentPalette(short paletteIndex);

/* Restores the offscreen image (and, for mono, the shade-level buffer
   Animate reads from) from an internally-cached copy of the current
   fractal's default view, if one exists and matches the offscreen
   store's current size - letting "Zoom Out" happen instantly instead
   of triggering a fresh render. Returns false, having changed
   nothing, if there's no usable cache for the current fractal (see
   width, extern'd elsewhere) - the caller should fall back to a
   normal RenderFractalOffscreen() in that case. Doesn't touch gView
   itself; call ResetViewForCurrentFractal() first. */
Boolean RestoreDefaultViewFromCache(void);
