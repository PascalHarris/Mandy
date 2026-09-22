/*****
 * mwWindow.h
 *
 *		Public interfaces for mwWindow.c
 *
 *****/
#ifndef _mwWindow_
#define _mwWindow_

/* width's sentinel for "no fractal type selected yet" - see its own
   comment in mwWindow.c for the full reasoning. Public because
   mwMenus.c's own gFractalMenuItemTypeID[] (SetUpMenus()) needs the
   same value, for a divider's placeholder slot in that table - not
   because width itself is meant to be compared against this from
   outside mwWindow.c in general (every other file that cares whether
   a fractal is selected already has a real function for that -
   HasRenderableImage(), IsZoomAvailable() - rather than reading width
   directly). */
#define kNoFractalSelectedWidth	0

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
   against - centreRe/centreIm is the view's middle, halfWidthRe half
   its width (visible Re range is centreRe ± halfWidthRe); the visible
   Im range follows from halfWidthRe scaled by the window's aspect
   ratio, so the view is never distorted regardless of zoom.
   
   double, not float: needs to keep meaningful precision once
   halfWidthRe has shrunk small from repeated zooming. How far it can
   actually shrink depends on gHasFPU (mwWindow.c) - see
   kFractalMinHalfWidthReDouble/Fixed (mwFractalMath.h), the two
   floors ClampHalfWidthRe() enforces. Going deeper on the gHasFPU
   path would need a perturbation-based approach for arbitrary depth,
   well beyond parameterising the viewport. */
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

/* A convenience one-off pixel-to-plane mapping, always in double
   regardless of gHasFPU - for callers that map occasionally (the
   marquee zoom feature, mwZoom.c; loading a saved view, mwSaveAs.c)
   rather than once per pixel. SampleMandelbrot()/SampleJulia()
   (mwWindow.c) don't use this for their own per-pixel hot path - see
   PrepareRenderMapping() there, and mwFractalMath.h's
   FractalMappingDouble/Fixed - since re-Preparing on every pixel
   would reintroduce exactly the per-call cost precomputing exists to
   avoid. */
void MapPixelToComplexPlane(short x, short y, double *outRe, double *outIm);

/* Clamps a proposed gView.halfWidthRe to a sensible range for the
   current fractal: a floor low enough to avoid precision collapse in
   the per-pixel iteration (mwWindow.c) - which floor depends on
   gHasFPU, see FractalView's own comment above - and a ceiling
   matching the current fractal's own default view, so repeated
   zooming out can't show an ever-larger, meaningless region. Used by
   both the marquee zoom feature's candidate view and keyboard zoom
   (+/-) - see mwZoom.c. */
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

/* Support for the Animate feature (mwColourCycle.c) -------------------
   Animation rotates the already-rendered image's colours/patterns in
   place rather than recomputing the fractal, so it stays cheap enough
   to run every couple of ticks. These expose just enough of this
   file's internals for mwColourCycle.c to do that: */

/* The offscreen colour GWorld's own colour table, for rotating its RGB
   entries directly (bypassing SetGWorld/the Palette Manager - see
   mwColourCycle.c for why). NULL if there's no colour offscreen store
   (no colour QuickDraw, or allocation failed under low memory). */
CTabHandle GetOffscreenColourTable(void);

/* How many of the offscreen colour table's entries mwColourCycle.c's
   RotateColourTable() may rotate - see the function's own comment in
   mwWindow.c for why this is less than the table's own full size. */
short GetRotatableColourTableEntryCount(void);

/* True if the current screen depth and gHasColourQD together mean
   fractals are actually being rendered in colour right now - i.e.
   ShouldRenderInColour(), exposed for mwColourCycle.c to decide which
   of colour-cycling or pattern-cycling applies. */
Boolean IsRenderingInColour(void);

/* True if Animate would actually do something useful right now.
   Colour cycling only needs the offscreen colour table to exist, so
   this always follows IsRenderingInColour() there. Mono pattern-cycling
   additionally needs gMonoShadeLevels to have both allocated
   successfully (see AllocateOffscreenMonoStore() - it's larger than
   the mono bitmap itself, so it's the more likely of the two to fail
   under real memory pressure) and to actually be populated - which,
   uniquely among the fractals, the Tree never does: it draws branches
   directly with MoveTo()/Line() rather than through ShadeBlock(), so
   there's no graduated shading to record or animate at all. */
Boolean IsAnimationAvailable(void);

/* Blits the whole current offscreen image to the window, exactly as
   an update event would - for mwColourCycle.c to call after rotating
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

/* True once a render has ever been allocated (regardless of whether
   it's still in progress, finished, or was aborted partway through) -
   false on a fresh launch, or if the one attempt failed under low
   memory. Used to gate Save As (both formats), zooming, and Animate -
   none of them mean anything against a window that's never actually
   rendered anything. */
Boolean HasRenderableImage(void);

/* True while zooming - in (mwZoom.c) or out (the Zoom Out menu item) -
   means anything right now: the current fractal (width) must actually
   have a zoomable view (Mandelbrot or Julia, not the Tree), and
   HasRenderableImage() must be true. */
Boolean IsZoomAvailable(void);

/* "New Fractal" (mwMenus.c): resets to the same "nothing selected
   yet" state the app launches into - disposes whatever's currently
   rendered and sets width back to its own initial sentinel, so the
   window goes blank until a fractal type is chosen again. */
void StartNewFractal(void);

/* Map width (the fractal-type selector) to and from its name as a
   plain C string, for saving/loading fractal parameters (see
   mwSaveAs.c). FractalTypeNameForWidth() returns "" for anything
   unrecognised; FindFractalTypeByName() returns false (leaving
   *outWidth untouched) for a name it doesn't recognise. */
const char *FractalTypeNameForWidth(short widthValue);
Boolean FindFractalTypeByName(const char *name, short *outWidth);

/* Which broad kind of fractal a type is - used to group the Fractal
   menu (mwMenus.c's SetUpMenus() builds it from this, rather than a
   hand-written string with one entry per type). kFractalFamilyRecursive
   fractals (the Tree) don't fit the per-pixel sampling model at all and
   are drawn directly instead - see RenderFractalOffscreen(). Note that
   family is NOT the same question as "does this type have a zoomable
   view" (see FractalTypeHasView() below) - Lyapunov is
   kFractalFamilyStatistical, not escape-time at all, but still has a
   real view (the a-b plane), just as zoomable as any escape-time
   type's complex plane. */
typedef enum {
	kFractalFamilyRecursive,
	kFractalFamilyEscapeTime,
	kFractalFamilyStatistical,
	kFractalFamilyConvergence
} FractalFamily;

FractalFamily FractalFamilyForWidth(short widthValue);

/* True for any type that uses gView at all - reusing the same pixel-
   to-plane mapping infrastructure regardless of what the two numbers
   it produces are then used for (a complex c or z for escape-time
   types; a and b for Lyapunov). This used to be answered by checking
   family == kFractalFamilyEscapeTime directly, in mwSaveAs.c, mwInfo.c,
   and several functions in this file - which was fine while escape-
   time was the only family with a view, but stopped being correct the
   moment Lyapunov arrived: it has just as real a view, on a different
   plane, without being escape-time. Currently true for every family
   except kFractalFamilyRecursive (the Tree, which draws a fixed shape
   with no view at all) - kept as its own named query rather than each
   caller re-deriving "family != recursive" itself, so a future family
   that ALSO has no view (if one ever exists) only needs updating here. */
Boolean FractalTypeHasView(short widthValue);

/* True only for a type with one fixed c the whole image shares (Julia
   today - width==3 was the literal check mwSaveAs.c/mwInfo.c used
   before this existed) as opposed to one that varies per pixel
   (Mandelbrot and its escape-time siblings, where c *is* the point
   under test) or has no such constant at all (the Tree). Distinct
   from FractalFamily deliberately: Burning Ship, Tricorn, Multibrot,
   and Phoenix are all kFractalFamilyEscapeTime but, like Mandelbrot,
   have no single constant worth reporting - this says which few types
   actually do, by name, not by family. */
Boolean FractalTypeHasFixedConstant(short widthValue);

/* Read-only iteration over the fractal type registry (mwWindow.c's
   kFractalTypes[]), for mwMenus.c's SetUpMenus() to build the Fractal
   menu's grouped type list from - in registry order, which is also
   menu order, so a divider belongs wherever FractalTypeFamilyAtIndex()
   changes between one index and the next. Deliberately a handful of
   accessors rather than exposing the whole FractalTypeDescriptor
   array: that struct's sampleProc field is a private implementation
   detail (FractalSampleProc itself isn't declared here at all), and
   nothing outside mwWindow.c needs it - a menu only needs a type's ID,
   name, and family. FractalTypeCount() is how far index may run (0..
   count-1); the other two are unspecified for anything outside that
   range. */
short FractalTypeCount(void);
short FractalTypeIDAtIndex(short index);
const char *FractalTypeNameAtIndex(short index);
FractalFamily FractalTypeFamilyAtIndex(short index);

/* True for a type that needs ShowParameterDialog() (mwParameterDialog.h)
   before it can actually render - Multibrot's power today, Lyapunov's
   driving sequence tomorrow. mwMenus.c's SetUpMenus() uses this to
   decide whether a type's own menu item needs the standard Mac
   trailing "..." marking "this opens a dialog before doing anything" -
   generic and automatic for whichever types actually need it, rather
   than mwMenus.c needing its own separate list to keep in sync with
   the registry by hand. */
Boolean FractalTypeNeedsConfigurationAtIndex(short index);

/* Runs widthValue's own configuration step (ShowParameterDialog(),
   mwParameterDialog.h) if it has one - Multibrot's power today - or
   does nothing and returns true immediately if it doesn't, so
   HandleMenu()'s fractalID case (mwMenus.c) can call this
   unconditionally before switching to any type, parameterized or not,
   without needing to know which is which itself. False means the
   person cancelled the dialog - the caller should leave width
   unchanged and not render, exactly as if the menu click never
   happened. */
Boolean ConfigureFractalTypeIfNeeded(short widthValue);

/* Multibrot's own power - see gMultibrotPower/ConfigureMultibrot() in
   mwWindow.c. Exposed so mwSaveAs.c can round-trip it through a saved
   .frct file (SetMultibrotPower() on load) the same way gView and the
   palette already are - without this, reloading a saved Multibrot
   fractal would silently render at whatever power happens to already
   be current rather than the one it was actually saved at.
   SetMultibrotPower() clamps rather than rejecting an out-of-range
   value - see its own comment in mwWindow.c. Narrow and type-specific
   rather than a general "every configurable parameter" mechanism:
   Multibrot is the only type with mutable, saveable configuration
   state so far, and generalising ahead of Lyapunov/Newton actually
   needing it risks guessing the wrong shape for something not
   designed yet. */
long GetMultibrotPower(void);
void SetMultibrotPower(long power);

/* Newton's own power - see gNewtonPower/ConfigureNewton() in
   mwWindow.c. Exposed for the same reason GetMultibrotPower()/
   SetMultibrotPower() are, and with the same clamp-not-reject
   behaviour on load. */
long GetNewtonPower(void);
void SetNewtonPower(long power);

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

/* Map a palette's index to and from its name, for saving/loading
   fractal parameters (see mwSaveAs.c). GetPaletteName() returns "" for
   an out-of-range index; FindPaletteByName() returns false (leaving
   *outIndex untouched) for a name it doesn't recognise. */
const char *GetPaletteName(short paletteIndex);
Boolean FindPaletteByName(const char *name, short *outIndex);

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

#endif	/* _mwWindow_ */
