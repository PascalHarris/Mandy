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
void HandleWindowResized(void);

/* Read-only access to render state, for the Get Info window (mwInfo.c). */
Boolean IsRenderActive(void);
unsigned long RenderElapsedTicks(void);
ConstStr255Param CurrentFractalName(void);
short CurrentScreenDepth(void);
void GetFractalResolution(short *outWidth, short *outHeight);

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

/* True if the current screen depth and gHasColorQD together mean
   fractals are actually being rendered in colour right now - i.e.
   ShouldRenderInColor(), exposed for mwColorCycle.c to decide which
   of colour-cycling or pattern-cycling applies. */
Boolean IsRenderingInColor(void);

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
