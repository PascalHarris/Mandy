/*****
 * mwWindow.c
 *
 *		The window routines for the Mandy Fractal Generator
 *
 *****/
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <QDOffscreen.h>
#include "mwWindow.h"
#include "mwFractalMath.h"	/* iteration, interior/shading maths - see that file */
#include "mwLyapunovMath.h"	/* IterateLyapunovExponent() - see SampleLyapunov() */
#include "mwNewtonMath.h"	/* IterateNewton()/NewtonRootIndex() - see SampleNewton() */
#include "mwParameterDialog.h"	/* ShowParameterDialog()/ParameterField - see ConfigureMultibrot() */
#ifndef _Quickdraw_
#include <Quickdraw.h>
#endif
#ifndef _FixMath_
#include <FixMath.h>	/* Fixed - Fixed-typed globals below still need this directly */
#endif

extern	Boolean	gHasColourQD;	/* set once in MandyWindow.c's InitMacintosh() */
extern	Boolean	gHasFPU;		/* set once in MandyWindow.c's InitMacintosh() */

#define windowX 0
#define windowY 40
#define pi 3.14159265

/* windowWidth/windowHeight - the content area's current size. These
   were #define constants (512x300) before the window became
   resizable (see HandleWindowResized()) - now runtime variables,
   updated there and read everywhere else in this file exactly as
   before, so a resize is visible everywhere that already reads them
   by name (fractal coordinate mapping, buffer sizing via imageStart,
   GetFractalResolution(), and so on) without those call sites needing
   to change at all. windowX/windowY stay fixed constants - they're
   only the window's initial on-screen position at launch, not
   involved in resizing (the window's actual position afterward,
   including after being dragged, is tracked by the Toolbox itself,
   not by this project). */
static short windowWidth  = 512;
static short windowHeight = 300;

/* Progressive-render tuning -------------------------------------------
   kBlockGridTargetColumns: the coarsest pass aims for about this many
   blocks across the longer side of the image (rounded down to a power
   of two), which is what gives a 512-wide window 4 columns.
   The finest pass size is NOT a fixed constant - see
   CurrentFinestBlockSize() - because it differs between colour and
   monochrome (colour refines all the way to real pixels; monochrome
   stops one level short, at 2x2, to leave room for a dither pattern
   simulating colour on a 1-bit screen).
   kBlocksPerIdleSlice: how many blocks AdvanceFractalRender() draws
   before yielding back to the event loop. Smaller keeps the app
   checking for input more often (smoother, more responsive); larger
   finishes a render sooner but leaves longer gaps between input
   checks - though real timing suggests that trade-off matters less
   than it looks: raising this from 4 to 16 alongside the adaptive
   iteration ceiling and float precision changes below took real
   render times from roughly 1000 seconds to roughly 250 - a large
   enough combined win that per-tick overhead clearly wasn't the
   limiting factor even at 16. Raised again to 32 on that basis, to
   re-test the balance now that the underlying cost per tick has
   dropped so much. Even at 32, a real render still yields many
   thousands of times over its course, so Command-period
   responsiveness shouldn't be noticeably affected - but this is a
   real trade-off, not a free win, and worth watching if the render
   ever feels unresponsive. */
#define kBlockGridTargetColumns	4
#define kBlocksPerIdleSlice		32

/* kBlitIntervalTicks: AdvanceFractalRender() used to call
   BlitOffscreenToWindow() after every single kBlocksPerIdleSlice
   batch - once per call, no exceptions. Real testing found an
   optimisation that should clearly have helped (skipping iteration
   entirely for the Mandelbrot set's main cardioid and period-2 bulb -
   see IsInMainCardioidOrBulb()) produced no visible speed difference
   at all. kBlocksPerIdleSlice's own comment above already shows fixed
   per-tick overhead isn't the bottleneck (raising it from 4 to 16 was
   a large part of an earlier ~4x win) - but CopyBits() itself scales
   with the *area* it copies, not a fixed per-call cost, and
   MapIndexToQuadrantOrder() scatters kBlocksPerIdleSlice blocks across
   the image by design, so their bounding rect - what
   BlitOffscreenToWindow() actually copies - can span most or all of
   the image even when only a small fraction of it changed in that
   batch. Throttling how often the blit actually happens, while still
   sampling at full speed underneath, targets that directly: 6 ticks
   (a tenth of a second) still looks smoothly progressive, but cuts
   the number of CopyBits() calls roughly sixfold for a render that
   would otherwise blit on every batch. AbortFractalRender()/a
   finished render still force one final blit regardless, so nothing
   ever finishes short of what it actually computed. */
#define kBlitIntervalTicks		6

/* kMinimumIterationCeiling: the floor UpdateIterationCeilingForBlockSize()
   won't reduce a coarse pass's ceiling below - see that function for
   why coarse passes get a reduced ceiling at all. kShadingScale, the
   common range SampleMandelbrot()/SampleJulia() report shade levels
   on regardless of which fractal's maxIterations produced them, now
   lives in mwFractalMath.h alongside ShadeLevelForIterationCount(),
   the function that actually produces values on that scale. */
#define kMinimumIterationCeiling	4

/* Mandelbrot and Julia's own natural default views - see FractalView
   in mwWindow.h - expressed so that, at gView's default, rendering
   matches this project's original fixed-zoom behaviour as closely as
   possible.
   
   Mandelbrot's matches exactly: the old code's zoom=150 meant
   windowWidth/zoom pixels-per-unit, i.e. a visible Re width of
   windowWidth/150 - halfWidthRe here is exactly half that, so the
   default view covers the identical region.
   
   Julia's Re range matches the old code's exactly (halfWidthRe=1.5
   reproduces the old 1.5*(x-256)/256 term precisely), but its Im
   range is very slightly different - about ±0.879 instead of the old
   ±1.0. The old code's Im scaling didn't actually follow the window's
   real 512:300 aspect ratio (1.5 wide by 1.0 tall isn't 512:300) -
   once halfWidthRe has to drive both axes consistently (so the
   marquee zoom feature's pixel-to-plane mapping in mwZoom.c stays
   correct at every zoom level, not just adds a special case for the
   very first one), the default view has to follow that same aspect-
   correct rule too, which shifts its vertical extent by about 12%. */
#define kMandelbrotDefaultCentreRe		-0.293333
#define kMandelbrotDefaultCentreIm		0.0
#define kMandelbrotDefaultHalfWidthRe	1.706667

#define kJuliaDefaultCentreRe			0.0
#define kJuliaDefaultCentreIm			0.0
#define kJuliaDefaultHalfWidthRe		1.5

/* Burning Ship's own default view - real -2.5..1.5, imaginary -1..2,
   matching the full-fractal framing widely cited for it (e.g.
   Wikimedia Commons' own "Burning Ship Fractal.png", lower-left
   (-2.5,-1), upper-right (1.5,2)) rather than reusing Mandelbrot's
   own (very different-shaped) default above. centreIm is positive
   because this project's own pixel-to-plane mapping already has Im
   increasing downward as pixel y increases (MapPixelToPlaneDouble()/
   Fixed(), mwFractalMath.c) - the same convention several of the
   sources above describe as giving the ship its traditional upright
   orientation. Worth checking against the actual rendered image
   regardless - if it comes out upside down, negating this value is
   the entire fix. */
#define kBurningShipDefaultCentreRe		-0.5
#define kBurningShipDefaultCentreIm		0.5
#define kBurningShipDefaultHalfWidthRe	2.0

/* Tricorn's own default view - the same box widely cited for both it
   and plain Mandelbrot's classic (not this project's own tuned)
   framing: real -2.5..1, imaginary -1..1 (e.g. HandWiki's Tricorn and
   Burning Ship articles both use this exact box in their reference
   pseudocode). Not reusing kMandelbrotDefaultCentreRe/Im/HalfWidthRe
   above - those were tuned for the plain Mandelbrot shape specifically,
   not verified to frame Tricorn's own, differently-proportioned
   three-cusped shape well. */
#define kTricornDefaultCentreRe			-0.75
#define kTricornDefaultCentreIm		0.0
#define kTricornDefaultHalfWidthRe		1.75

/* Multibrot's own default view - a generic, conservative Mandelbrot-
   like framing rather than this project's own tuned one, since it
   hasn't been verified (unlike Burning Ship/Tricorn above, sourced
   from cited reference images) to frame every power 3-5 well; picked
   to be safely unlikely to show an empty view for any of them, not
   tuned for any one. */
#define kMultibrotDefaultCentreRe		-0.5
#define kMultibrotDefaultCentreIm		0.0
#define kMultibrotDefaultHalfWidthRe	1.5

/* Phoenix's own default view - a generic, safe Mandelbrot-scale
   framing, the same reasoning as Multibrot's own above: not sourced
   from any specific cited rendering of this fractal (unlike Burning
   Ship/Tricorn), just picked to be unlikely to show an empty view. Its
   iteration keeps the same real-axis mirror symmetry plain Mandelbrot
   has (p is real), so centreIm=0 is still the sensible choice here. */
#define kPhoenixDefaultCentreRe			-0.5
#define kPhoenixDefaultCentreIm		0.0
#define kPhoenixDefaultHalfWidthRe		1.5

/* Lyapunov's own default view - not a complex plane at all (see
   mwLyapunovMath.h's own comment), but the same gView/pixel-mapping
   machinery reused for its a-b parameter plane instead: centreRe/Im
   here are a and b's own centres, halfWidthRe their own half-range.
   [2.5, 4.0] on both axes is the standard "interesting" region for the
   driven logistic map - below about 2.5 either parameter just
   converges to a stable fixed point with nothing structurally
   interesting to see, and 4.0 is the map's own upper bound (x leaves
   [0,1] above it). This is a real, well-established range for this
   fractal specifically, not a generic placeholder the way Multibrot's/
   Phoenix's own defaults above are. */
#define kLyapunovDefaultCentreRe		3.25
#define kLyapunovDefaultCentreIm		3.25
#define kLyapunovDefaultHalfWidthRe		0.75

/* Newton's own default view - the polynomial's own roots (the power-th
   roots of unity) all sit exactly on the unit circle regardless of
   power, so a view comfortably larger than that circle (real -2..2,
   scaled to the window's own aspect for the imaginary axis) shows every
   basin's own structure for any power this fractal offers - a real,
   geometry-derived choice, not a generic placeholder the way
   Multibrot's/Phoenix's own defaults are. kNewtonMaxIterations: Newton's
   method converges quadratically, so most points settle in well under
   10 iterations - 32 gives real headroom without the coarse-pass
   iteration-ceiling reduction (UpdateIterationCeilingForBlockSize())
   needing to matter much either way for this fractal. */
#define kNewtonDefaultCentreRe			0.0
#define kNewtonDefaultCentreIm			0.0
#define kNewtonDefaultHalfWidthRe		2.0
#define kNewtonMaxIterations			32

#define kMandelbrotMaxIterations	64

#define kJuliaConstantRe		-0.7
#define kJuliaConstantIm		0.27015
#define kJuliaMaxIterations		300

/* Fixed-point equivalents of kJuliaConstantRe/Im, for
   IterateEscapeTimeFixed() on the !gHasFPU path. Written as plain
   integer literals rather than a DoubleToFixed(kJuliaConstantRe)-style
   macro: that would textually re-expand to a floating-point multiply
   at every use site, and while a good optimizer would constant-fold
   two compile-time literals like that down to nothing, relying on
   Think C actually doing so - rather than genuinely re-running it once
   per pixel in SampleJulia(), reintroducing exactly the floating point
   this path exists to avoid - isn't a chance worth taking for two
   values that never change. -45875 and 17704 are -0.7 and 0.27015
   each multiplied by 65536.0 and truncated toward zero, matching what
   (Fixed) casting the double would produce; computed with a script
   rather than by hand to keep the arithmetic itself trustworthy. */
#define kJuliaConstantReFixed	((Fixed) -45875)
#define kJuliaConstantImFixed	((Fixed) 17704)

/* Window title shown while idle, versus while a progressive render is
   under way. "\021" is the Command-key glyph (Mac OS Roman code 0x11,
   the same character AppendMenu()'s "/" syntax draws automatically in
   menus) - it displays correctly in the title bar's system font on
   any real Mac. Swap in a plain "Cmd-." if that glyph ever turns out
   not to render as expected. */
#define kIdleWindowTitle		"\pFractal Window"
#define kRenderingWindowTitle	"\pFractal Window (\021. to abort)"

WindowPtr	mwWindow;
Rect		dragRect;
/* windowBounds/imageStart's initial values are written out literally
   (matching windowWidth/windowHeight's own initial values above)
   rather than computed from those variables, since C requires a
   static initializer to be a compile-time constant - a plain variable
   reference, even one that never actually changes before this line
   runs, isn't allowed here. HandleWindowResized() updates both
   directly, by assignment, on every actual resize. */
Rect		windowBounds = { windowY, windowX, windowY+300, windowX+512 };
Rect		imageStart = {0, 0, 300, 512};

/* width doubles as the fractal-type selector - see kFractalTypes[]
   below for which ID is which - and, before the person has ever
   picked one, kNoFractalSelectedWidth (mwWindow.h), a sentinel meaning
   "nothing selected yet". 0 rather than one past the last real type
   (which is what this used to be, back when there were only ever
   three types to be "one past"): a fixed offset like that collides
   the moment a type gets added at that same ID, which is exactly what
   very nearly happened when Burning Ship arrived - 0 is guaranteed
   distinct from every real type's ID regardless of how many exist,
   since real IDs start at 1 and DescriptorForWidth() has nothing to
   look up for it, so RenderFractalOffscreen() and friends all
   correctly do nothing, leaving the window blank exactly as it is on
   a fresh launch. StartNewFractal() (mwMenus.c's "New Fractal") resets
   back to this same value. */
int			width = kNoFractalSelectedWidth;

/* The current Mandelbrot/Julia view - see FractalView in mwWindow.h.
   Initialised to Mandelbrot's own default so it's never garbage even
   before the very first ResetViewForCurrentFractal() call (which
   always happens before either fractal is ever rendered - see
   mwMenus.c - but this costs nothing to have anyway). */
FractalView	gView = { kMandelbrotDefaultCentreRe, kMandelbrotDefaultCentreIm, kMandelbrotDefaultHalfWidthRe };

/* Forward declaration only - see the full definition and kFractalTypes[]
   itself further down this file (after the sample functions each row's
   sampleProc field points to are forward-declared). A typedef to an
   incomplete struct is fine to use as an opaque pointer, which is all
   every caller before that point needs - but NOT to dereference a
   member through, which needs the full definition visible at the
   point of the dereference, not just at the point of the call. That
   distinction is exactly what caught ResetViewForCurrentFractal()/
   MaximumHalfWidthReForCurrentFractal()/
   IsCurrentViewTheDefaultForCurrentFractal() out - all three actually
   read a descriptor's own fields, not just pass the pointer around, so
   all three had to move below the real struct definition instead of
   living up here where it would have been more natural to group them
   with ResetViewForCurrentFractal()'s own public declaration. Real
   testing (an actual compile) is what caught this - none of the checks
   this project could run without a working toolchain (brace/paren
   balance, comment pairing) can catch a type-completeness error, since
   it's a property of the language's own rules, not the text's shape. */
typedef struct FractalTypeDescriptor FractalTypeDescriptor;
static const FractalTypeDescriptor *DescriptorForWidth(short widthValue);
static double  MaximumHalfWidthReForCurrentFractal(void);
static Boolean IsCurrentViewTheDefaultForCurrentFractal(void);

/* MapPixelToComplexPlane()
   See mwWindow.h. A convenience wrapper for mwFractalMath.h's own
   Prepare/Map pair, for callers (mwZoom.c's marquee, mwSaveAs.c) that
   only need an occasional one-off mapping and can afford to Prepare
   fresh every call - unlike SampleMandelbrot()/SampleJulia()'s own
   per-pixel hot path, which reuses gRenderMappingDouble/Fixed,
   Prepared once per render (see PrepareRenderMapping()). Always
   double, regardless of gHasFPU: the marquee only ever runs once per
   drag, not once per pixel, so there's no case here for Fixed's speed
   at the cost of its precision. */
void MapPixelToComplexPlane(short x, short y, double *outRe, double *outIm) {
	FractalMappingDouble mapping;
	
	PrepareFractalMappingDouble(&mapping, &gView, windowWidth, windowHeight);
	MapPixelToPlaneDouble(&mapping, x, y, outRe, outIm);
}

/* ClampHalfWidthRe()
   See mwWindow.h. Picks between mwFractalMath.h's two precision
   floors by gHasFPU - see their own comment there for why they
   differ. This is the only place that distinction needs to be made:
   every other caller (zoom in/out, marquee, FRCT load) reaches its
   own halfWidthRe only through this function. MaximumHalfWidthReForCurrentFractal()
   itself is defined later in this file (after kFractalTypes[]'s own
   full definition, which it needs to dereference) - its forward
   declaration above is enough for this call. */
double ClampHalfWidthRe(double proposedHalfWidthRe) {
	double maximum = MaximumHalfWidthReForCurrentFractal();
	double minimum = gHasFPU ? kFractalMinHalfWidthReDouble : kFractalMinHalfWidthReFixed;
	
	if (proposedHalfWidthRe < minimum)
		return minimum;
	if (proposedHalfWidthRe > maximum)
		return maximum;
	
	return proposedHalfWidthRe;
}

/* Offscreen pixel store --------------------------------------------
   The progressive renderer draws into this buffer; DrawContent() then
   just copies finished pixels onto the screen. Two different
   technologies back it depending on gHasColourQD:
   
   - Monochrome: a plain BitMap with a manually allocated
     baseAddr/rowBytes, wrapped in an ordinary GrafPort. This is the
     classic pre-Color QuickDraw offscreen-bitmap technique, so it
     works unmodified on real Mac Plus hardware.
   - Colour: an 8-bit indexed GWorld with a small custom colour table
     (see BuildFractalColourTable()) built to hold a smooth ramp across
     kShadingScale. 8-bit indexed, rather than matching the screen's
     actual depth, is deliberate: CopyBits() automatically dithers
     this down to whatever the real screen supports (4-bit and up),
     and an indexed image is what a future palette-cycling animation
     (the "trippy" effect on the roadmap) needs to rewrite cheaply.
   
   Only one of offscreenPort/offscreenBits or offscreenGWorld is ever
   live at a time, selected by gHasColourQD; offscreenBounds and
   offscreenReady describe whichever one is current. */
static GrafPort		offscreenPort;
static BitMap		offscreenBits;
static GWorldPtr	offscreenGWorld;
static Rect			offscreenBounds;
static Boolean		offscreenReady = false;

/* Mono pattern-cycling support (see ApplyMonoPatternPhase()) --------
   One byte per finest-size cell (CurrentFinestBlockSize() when not
   rendering in colour), recording the shade level ShadeBlock() last
   drew there - a coarse pass records the same level into every finest
   cell under it, which a later, finer pass then overwrites with more
   accurate values, so by the time a render completes every entry
   reflects the actual final image, exactly like the pixels themselves.
   Allocated unconditionally alongside the mono offscreen store itself
   (see AllocateOffscreenMonoStore()) - most runs never turn animation
   on and never read this, but it costs little to always have it ready
   and already populated by the time they do. */
static unsigned char	*gMonoShadeLevels = NULL;
static short			gMonoShadeLevelColumns;
static short			gMonoShadeLevelRows;

/* Default-view cache, for instant "Zoom Out" ------------------------
   Caches the offscreen image - and, for mono, gMonoShadeLevels
   alongside it, so Animate keeps working correctly on a restored
   cache rather than redrawing from shade levels left over from
   whatever zoomed view was rendered most recently - the moment a
   render of the current fractal's own default view (see
   ResetViewForCurrentFractal()) finishes naturally. See
   CacheOffscreenAsDefaultViewIfApplicable(), called from
   BeginNextPass() at exactly that point - not from an aborted render
   (AbortFractalRender()), and not for the Tree, which doesn't use
   gView at all.
   
   One slot only, sized for whichever fractal is currently selected -
   switching fractals overwrites it with a fresh cache for the newly
   selected one the moment its own default view finishes rendering,
   which happens immediately on every fractal switch (see mwMenus.c),
   so there's never a need to cache more than one fractal's default at
   once. gDefaultViewCacheWidth (matched against width, this file's
   own global) records which fractal the cache is actually for, so a
   restore attempt for the wrong one is refused rather than showing
   the wrong image. */
static Ptr				gDefaultViewCachePixels = NULL;
static long				gDefaultViewCachePixelsSize = 0;
static unsigned char	*gDefaultViewCacheShadeLevels = NULL;
static short			gDefaultViewCacheWidth = 0;

/* CacheOffscreenAsDefaultViewIfApplicable()
   Snapshots the offscreen image (and, for mono, gMonoShadeLevels) into
   the default-view cache, if the render that just finished was for
   the current fractal's own default view - called only from
   BeginNextPass()'s natural-completion branch, so an aborted render
   never gets cached. A failed allocation just leaves the cache
   invalid (gDefaultViewCacheWidth left not matching width) rather
   than caching something partial - RestoreDefaultViewFromCache()
   already falls back to a full render whenever the cache doesn't
   apply, so there's nothing else to do here on failure. */
static void CacheOffscreenAsDefaultViewIfApplicable(void) {
	BitMap	*bits;
	Rect	bounds;
	long	pixelsSize;
	
	if (!IsCurrentViewTheDefaultForCurrentFractal())
		return;
	
	if (!GetOffscreenImage(&bits, &bounds))
		return;
	
	pixelsSize = (long) bits->rowBytes * (bounds.bottom - bounds.top);
	
	if (gDefaultViewCachePixels == NULL || gDefaultViewCachePixelsSize != pixelsSize) {
		if (gDefaultViewCachePixels != NULL)
			DisposePtr(gDefaultViewCachePixels);
		
		gDefaultViewCachePixels     = NewPtr(pixelsSize);
		gDefaultViewCachePixelsSize = pixelsSize;
	}
	
	if (gDefaultViewCachePixels == NULL) {
		gDefaultViewCacheWidth = 0;
		return;
	}
	
	BlockMove(bits->baseAddr, gDefaultViewCachePixels, pixelsSize);
	
	if (!gHasColourQD && gMonoShadeLevels != NULL) {
		long shadeLevelsSize = (long) gMonoShadeLevelColumns * gMonoShadeLevelRows;
		
		if (gDefaultViewCacheShadeLevels == NULL)
			gDefaultViewCacheShadeLevels = (unsigned char *) NewPtr(shadeLevelsSize);
		
		if (gDefaultViewCacheShadeLevels != NULL)
			BlockMove(gMonoShadeLevels, gDefaultViewCacheShadeLevels, shadeLevelsSize);
	}
	
	gDefaultViewCacheWidth = width;
}

/* A fractal sample function reports how "escaped" the point at (x,y)
   is, on the shared kShadingScale range - see SampleMandelbrot() and
   SampleJulia(). A fractal configure proc shows whatever
   ShowParameterDialog() (mwParameterDialog.h) call a type needs before
   it can render at all, returning false if the person cancelled -
   NULL for every type that doesn't need one. A fractal direct-draw
   proc draws a type that doesn't sample at all (the Tree; eventually
   Fern/Sierpinski) - zero arguments deliberately, so this table can
   dispatch through one function pointer type regardless of what
   parameters any one type's own drawing function actually needs
   internally (DrawBranch()'s x/y/angle/depth aren't meaningful for an
   IFS fractal at all) - see DrawTreeOffscreen()/DrawTreeDirectly(). */
typedef short (*FractalSampleProc)(short x, short y);
typedef Boolean (*FractalConfigureProc)(void);
typedef void (*FractalDirectDrawProc)(void);

/* Forward declarations for kFractalTypes[] below, which references
   these by name for its sampleProc/configureProc columns before any
   of them are actually defined further down this file - a plain
   identifier used this way (not as a call) needs a prior declaration
   to be valid C at all, not just a style preference the way forward-
   declaring an ordinary called function often is. */
static short		SampleMandelbrot(short x, short y);
static short		SampleJulia(short x, short y);
static short		SampleBurningShip(short x, short y);
static short		SampleTricorn(short x, short y);
static short		SampleMultibrotConfigurable(short x, short y);
static short		SamplePhoenix(short x, short y);
static short		SampleLyapunov(short x, short y);
static Boolean		ConfigureLyapunov(void);
static short		SampleNewton(short x, short y);
static Boolean		ConfigureNewton(void);
static Boolean		ConfigureMultibrot(void);
static void			DrawTreeOffscreen(void);
static void			DrawTreeDirectly(void);
static void			DrawFernOffscreen(void);
static void			DrawFernDirectly(void);
static void			DrawSierpinskiOffscreen(void);
static void			DrawSierpinskiDirectly(void);

/* One row per fractal type - name, family (menu grouping - see
   mwMenus.c's SetUpMenus() - and FractalFamilyForWidth()), which
   function actually samples it (NULL for a direct-draw type - the
   Tree, and eventually Fern/Sierpinski - which uses directDrawProc/
   directDrawDirectProc instead, see RenderFractalOffscreen()/
   DrawFractalDirectly()), its own iteration ceiling, its own default
   view, its fixed constant if it has one (Julia only, so far - see
   FractalTypeHasFixedConstant()), its own configuration step if it
   needs one (Multibrot's power, via ConfigureMultibrot() - see
   FractalTypeNeedsConfigurationAtIndex()/ConfigureFractalTypeIfNeeded()),
   and its own pair of direct-draw functions if it doesn't sample at
   all (the Tree's DrawTreeOffscreen()/DrawTreeDirectly(), thin
   wrappers around DrawBranch()/DrawBranchDirectly() so this table can
   dispatch through a plain zero-argument function pointer regardless
   of what parameters any one type's own drawing function actually
   needs internally). This replaces what used to be five separate
   width==1/2/3 chains (FractalTypeNameForWidth(), FindFractalTypeByName(),
   ResetViewForCurrentFractal(), MaximumHalfWidthReForCurrentFractal(),
   RenderFractalOffscreen()/DrawFractalDirectly()'s own dispatch, and
   GetFractalParameters()) - each one a place a new fractal type could
   be added to some but not all of, silently. One table now, read by
   DescriptorForWidth() below; adding a type is one new row.

   typeID values are stable identifiers, not menu positions - nothing
   here assumes typeID N sits at Fractal-menu item N (see
   mwMenus.c's own comment on why that assumption broke). The values
   themselves don't need to mean anything beyond "distinct" - existing
   saved .frct files already carry the type by name (see
   FindFractalTypeByName()), not by this number, so renumbering later
   costs nothing. */
/* FractalSymmetryKind - whether, and how, a type's escape/convergence
   result for a point is provably identical to its result for some
   OTHER point derivable from it, given the CURRENT view happens to
   sample both - see SampleWithSymmetryFold()'s own, much longer
   comment for the mathematics, the exact view condition each kind
   needs, and why this is fundamentally different from (and safer
   than) the Mariani-Silver attempts documented above: those were
   heuristics that could be wrong; this is an algebraic identity,
   proven by induction for every type it's set on below, not assumed.

   kFractalSymmetryNone: no exploitable symmetry, or none proven -
   Burning Ship included deliberately (its own abs() operations break
   the conjugate relationship every other Mandelbrot-shaped type here
   has - confirmed against multiple independent sources, not just
   derived), and Lyapunov/Fern/Sierpinski/Tree, none of which this
   comment's own reasoning applies to at all.

   kFractalSymmetryRealAxis: c and conj(c) give identical results -
   Mandelbrot, Tricorn, Multibrot, Phoenix, and Newton all qualify (see
   SampleWithSymmetryFold()'s own comment for the per-type proof
   sketch), whenever the current view's own centreIm is exactly 0.0. */
typedef enum {
	kFractalSymmetryNone,
	kFractalSymmetryRealAxis
} FractalSymmetryKind;

struct FractalTypeDescriptor {
	short					typeID;
	const char				*name;
	FractalFamily			family;
	FractalSampleProc		sampleProc;
	short					maxIterations;
	double					defaultCentreRe;
	double					defaultCentreIm;
	double					defaultHalfWidthRe;
	Boolean					hasFixedConstant;
	double					constantRe;
	double					constantIm;
	FractalConfigureProc	configureProc;
	FractalDirectDrawProc	directDrawProc;
	FractalDirectDrawProc	directDrawDirectProc;
	FractalSymmetryKind		symmetryKind;
};

static const FractalTypeDescriptor kFractalTypes[] = {
	{ 1, "Tree",         kFractalFamilyRecursive,  NULL,                       0,                        0.0, 0.0, 0.0, false, 0.0, 0.0, NULL,               DrawTreeOffscreen, DrawTreeDirectly, kFractalSymmetryNone },
	{ 10, "Barnsley Fern", kFractalFamilyRecursive, NULL,                      0,                        0.0, 0.0, 0.0, false, 0.0, 0.0, NULL,               DrawFernOffscreen, DrawFernDirectly, kFractalSymmetryNone },
	{ 11, "Sierpinski",  kFractalFamilyRecursive,  NULL,                       0,                        0.0, 0.0, 0.0, false, 0.0, 0.0, NULL,               DrawSierpinskiOffscreen, DrawSierpinskiDirectly, kFractalSymmetryNone },
	{ 2, "Mandelbrot",   kFractalFamilyEscapeTime, SampleMandelbrot,           kMandelbrotMaxIterations, kMandelbrotDefaultCentreRe, kMandelbrotDefaultCentreIm, kMandelbrotDefaultHalfWidthRe, false, 0.0, 0.0, NULL,               NULL, NULL, kFractalSymmetryRealAxis },
	{ 3, "Julia",        kFractalFamilyEscapeTime, SampleJulia,                kJuliaMaxIterations,      kJuliaDefaultCentreRe, kJuliaDefaultCentreIm, kJuliaDefaultHalfWidthRe, true, kJuliaConstantRe, kJuliaConstantIm, NULL,               NULL, NULL, kFractalSymmetryNone },
	{ 4, "Burning Ship", kFractalFamilyEscapeTime, SampleBurningShip,          kMandelbrotMaxIterations, kBurningShipDefaultCentreRe, kBurningShipDefaultCentreIm, kBurningShipDefaultHalfWidthRe, false, 0.0, 0.0, NULL,               NULL, NULL, kFractalSymmetryNone },
	{ 5, "Tricorn",      kFractalFamilyEscapeTime, SampleTricorn,              kMandelbrotMaxIterations, kTricornDefaultCentreRe, kTricornDefaultCentreIm, kTricornDefaultHalfWidthRe, false, 0.0, 0.0, NULL,               NULL, NULL, kFractalSymmetryRealAxis },
	{ 6, "Multibrot",    kFractalFamilyEscapeTime, SampleMultibrotConfigurable, kMandelbrotMaxIterations, kMultibrotDefaultCentreRe, kMultibrotDefaultCentreIm, kMultibrotDefaultHalfWidthRe, false, 0.0, 0.0, ConfigureMultibrot, NULL, NULL, kFractalSymmetryRealAxis },
	{ 7, "Phoenix",      kFractalFamilyEscapeTime, SamplePhoenix,               kMandelbrotMaxIterations, kPhoenixDefaultCentreRe, kPhoenixDefaultCentreIm, kPhoenixDefaultHalfWidthRe, false, 0.0, 0.0, NULL,               NULL, NULL, kFractalSymmetryRealAxis },
	{ 8, "Lyapunov",     kFractalFamilyStatistical, SampleLyapunov,             0,                        kLyapunovDefaultCentreRe, kLyapunovDefaultCentreIm, kLyapunovDefaultHalfWidthRe, false, 0.0, 0.0, ConfigureLyapunov,  NULL, NULL, kFractalSymmetryNone },
	{ 9, "Newton",       kFractalFamilyConvergence, SampleNewton,               kNewtonMaxIterations,     kNewtonDefaultCentreRe, kNewtonDefaultCentreIm, kNewtonDefaultHalfWidthRe, false, 0.0, 0.0, ConfigureNewton,    NULL, NULL, kFractalSymmetryRealAxis }
};
#define kFractalTypeCount	(sizeof(kFractalTypes) / sizeof(kFractalTypes[0]))

/* DescriptorForWidth()
   The one row matching widthValue, or NULL for kNoFractalSelectedWidth
   or anything else this build doesn't have - every caller below
   already checks for NULL rather than assuming a match, the same
   caution FractalTypeNameForWidth()'s own comment already called for
   before this table existed. */
static const FractalTypeDescriptor *DescriptorForWidth(short widthValue) {
	short i;
	
	for (i = 0; i < (short) kFractalTypeCount; i++) {
		if (kFractalTypes[i].typeID == widthValue)
			return &kFractalTypes[i];
	}
	
	return NULL;
}

/* ResetViewForCurrentFractal()
   See mwWindow.h. Does nothing for the Tree (no descriptor row - it
   has no view at all) or an unrecognised width, exactly as the old
   width==2/3 chain this replaced did for anything other than
   Mandelbrot or Julia. Defined here, after kFractalTypes[]'s own full
   definition above, rather than up near gView where it would read
   more naturally next to its own declaration in mwWindow.h - it
   dereferences a descriptor's own fields, which needs the complete
   struct visible at the point of the dereference itself, not just a
   forward-declared pointer to it (real testing - an actual compile -
   caught this out; see the forward-declaration comment further up
   this file for the full story). */
void ResetViewForCurrentFractal(void) {
	const FractalTypeDescriptor *descriptor = DescriptorForWidth(width);
	
	if (descriptor == NULL || !FractalTypeHasView(width))
		return;
	
	gView.centreRe    = descriptor->defaultCentreRe;
	gView.centreIm    = descriptor->defaultCentreIm;
	gView.halfWidthRe = descriptor->defaultHalfWidthRe;
}

/* MaximumHalfWidthReForCurrentFractal()
   The current fractal's own default halfWidthRe - the ceiling
   ClampHalfWidthRe() enforces, so zooming out repeatedly can't show an
   ever-larger, eventually meaningless region beyond what the fractal
   was ever meant to be viewed at. Falls back to Mandelbrot's own
   default for the Tree or an unrecognised width - shouldn't be
   reached in practice, since callers check IsZoomAvailable() first,
   but returning a sensible, real value here instead of leaving this
   undefined for a caller that doesn't check first, does no harm.
   Defined here rather than next to ClampHalfWidthRe() itself, for the
   same struct-completeness reason as ResetViewForCurrentFractal()
   above. */
static double MaximumHalfWidthReForCurrentFractal(void) {
	const FractalTypeDescriptor *descriptor = DescriptorForWidth(width);
	
	if (descriptor != NULL && FractalTypeHasView(width))
		return descriptor->defaultHalfWidthRe;
	
	return kMandelbrotDefaultHalfWidthRe;
}

/* IsCurrentViewTheDefaultForCurrentFractal()
   True if gView currently holds exactly the current fractal's own
   default view - an exact floating-point comparison against the same
   literal constants ResetViewForCurrentFractal() assigns from this
   same table, which is safe here because gView only ever holds one of
   these exact literals, or a value computed by the marquee zoom
   feature's interpolation (mwZoom.c), which would only match by the
   most remote coincidence. False for the Tree (no descriptor row) or
   an unrecognised width, same as ResetViewForCurrentFractal(). Defined
   here rather than next to CacheOffscreenAsDefaultViewIfApplicable()
   itself, for the same struct-completeness reason as
   ResetViewForCurrentFractal() above. */
static Boolean IsCurrentViewTheDefaultForCurrentFractal(void) {
	const FractalTypeDescriptor *descriptor = DescriptorForWidth(width);
	
	if (descriptor == NULL || !FractalTypeHasView(width))
		return false;
	
	return gView.centreRe    == descriptor->defaultCentreRe
			&& gView.centreIm    == descriptor->defaultCentreIm
			&& gView.halfWidthRe == descriptor->defaultHalfWidthRe;
}

/* FractalTypeCount()/FractalTypeIDAtIndex()/FractalTypeNameAtIndex()/
   FractalTypeFamilyAtIndex()
   See mwWindow.h. Index isn't bounds-checked - every caller is
   mwMenus.c's SetUpMenus(), looping 0..FractalTypeCount()-1 itself. */
short FractalTypeCount(void) {
	return (short) kFractalTypeCount;
}

short FractalTypeIDAtIndex(short index) {
	return kFractalTypes[index].typeID;
}

const char *FractalTypeNameAtIndex(short index) {
	return kFractalTypes[index].name;
}

FractalFamily FractalTypeFamilyAtIndex(short index) {
	return kFractalTypes[index].family;
}

Boolean FractalTypeNeedsConfigurationAtIndex(short index) {
	return kFractalTypes[index].configureProc != NULL;
}

/* ConfigureFractalTypeIfNeeded()
   See mwWindow.h. Looks widthValue up itself (rather than taking a
   descriptor pointer) since HandleMenu() (mwMenus.c) - the only
   caller - only ever has a type ID at this point, not a pointer into
   a table it doesn't have access to. */
Boolean ConfigureFractalTypeIfNeeded(short widthValue) {
	const FractalTypeDescriptor *descriptor = DescriptorForWidth(widthValue);
	
	if (descriptor == NULL || descriptor->configureProc == NULL)
		return true;
	
	return descriptor->configureProc();
}

/* The iteration ceiling SampleMandelbrot()/SampleJulia() actually use
   for whatever block is currently being sampled - see
   UpdateIterationCeilingForBlockSize(). Explicitly set by every
   caller of either sampler (RenderFractalOffscreen()'s pass
   transitions, and DrawFractalDirectly()'s fallback path) rather
   than derived implicitly from fractalRenderJob state, since
   DrawFractalDirectly() runs with no progressive job - and hence no
   meaningful fractalRenderJob.blockSize - at all. */
static short currentIterationCeiling;

/* gRenderMapping{Double,Fixed} - the pixel-to-plane mapping for
   whichever fractal is currently rendering, precomputed once per
   render (PrepareRenderMapping(), called from both
   StartProgressiveRender() and DrawFractalDirectly()) rather than
   re-derived per pixel - see mwFractalMath.h's own comment on why
   this matters. Both are always prepared, not just whichever gHasFPU
   would select: most sample functions only ever read the one that
   matches gHasFPU, exactly as they dispatch on it for everything
   else, but Lyapunov and Newton are always-double regardless of
   gHasFPU (see IterateLyapunovExponent()'s own comment on why) and
   need gRenderMappingDouble to be valid even on non-FPU hardware.
   Preparing the one a given render won't actually use costs a few
   divisions, once per render, not per pixel - negligible next to
   anything else here. Distinct from MapPixelToComplexPlane()'s own,
   freshly-Prepared-per-call mapping (mwZoom.c/mwSaveAs.c's occasional
   use) - these two never need to agree on freshness since each caller
   Prepares its own. */
static FractalMappingDouble	gRenderMappingDouble;
static FractalMappingFixed		gRenderMappingFixed;

static void PrepareRenderMapping(void) {
	PrepareFractalMappingDouble(&gRenderMappingDouble, &gView, windowWidth, windowHeight);
	PrepareFractalMappingFixed(&gRenderMappingFixed, &gView, windowWidth, windowHeight);
}

/* SampleWithSymmetryFold() -------------------------------------------
   The mathematics: for a type with kFractalSymmetryRealAxis
   (mwWindow.h's own comment on the enum lists which), the escape or
   convergence result for c is provably identical to the result for
   conj(c) - proven here by induction for each type that carries the
   flag, not assumed:

     Mandelbrot/Multibrot (z -> z^n+c): if z(k) is c's own orbit, then
     conj(z(k)) is conj(c)'s orbit, since conj(z^n+c) = conj(z)^n+conj(c)
     for any integer n - conjugation commutes with both raising to a
     power and addition. So |z(k)| = |conj(z(k))| at every step, and
     the two orbits escape (or don't) at exactly the same iteration.

     Tricorn (z -> conj(z)^2+c): a slightly different induction (see
     mwFractalMath.c's own git history/commit reasoning if this is ever
     revisited) shows conj(c)'s orbit is the conjugate of c's own, one
     step delayed in how the conjugate gets reapplied - the escape time
     still comes out identical either way.

     Phoenix (z -> z^2+c+p*zPrev, p REAL): conjugating both z(k) and
     zPrev(k) together is preserved by the update, precisely because p
     has no imaginary part (conj(p*x) = p*conj(x) only when p is real) -
     this project's own p=-0.5 (Ushiki's classic value) satisfies that.

     Newton (z -> z - (z^n-1)/(n*z^(n-1))): z^n-1 has real coefficients,
     so conj(f(z)) = f(conj(z)) for Newton's own update f - meaning
     conj(z0) converges in exactly as many steps as z0 does, to
     whichever root is conj(z0)'s own converged root's conjugate (not
     necessarily the SAME root, unless it's the real one) - NOT
     currently exploited here despite qualifying: SampleNewton() would
     need to remap the cached root index to its own conjugate root, not
     just reuse the cached shade level outright the way the other four
     types can, and that remapping isn't implemented yet. Newton is
     deliberately left off the symmetryKind list above until it is,
     rather than marked eligible and produce a wrong shade for anyone
     who actually zooms out enough to see two symmetric basins mixed up.

   Burning Ship does NOT qualify - confirmed against multiple
   independent published sources, not just derived here: its own
   abs(Re)/abs(Im) step breaks the clean conjugate relationship the
   moment either component is nonzero, which is essentially always.

   This is categorically different from - and safer than - the
   Mariani-Silver attempts DrawNextBlockAndAdvance()'s own comment
   documents: those were heuristics (assume a block is uniform from a
   handful of border samples) that could be, and were, wrong on real
   testing. This is a proven algebraic identity: c and conj(c) don't
   just *probably* match, they always do, for every type flagged above.
   The only real risk here is a bookkeeping bug in the cache itself, not
   the underlying maths being unsound - which is exactly why this is
   verified against a plain reference (see the .c file this shipped
   alongside) before being trusted.

   The mechanism: whichever of a mirror pair (x,y)/(x, windowHeight-y)
   is sampled FIRST in this pass computes normally and records its
   shade level; the second one, whenever it's actually visited, finds
   that value already cached and reuses it instead of sampling at all.
   Quadrant order (MapIndexToQuadrantOrder()) doesn't process either
   half of a pair in any guaranteed order, so the cache has to work
   correctly regardless of which one arrives first - indexing by
   min(y, windowHeight-y) (see gSymmetryCacheRowForY()) does that: both
   members of a pair always land on the same cache row, whichever one
   gets there first.

   Applied ONLY at the finest, one-pixel-per-block pass
   (fractalRenderJob.blockSize == 1, colour only - mono's own finest is
   2x2, see CurrentFinestBlockSize(), and a 2x2 cell's own centre point
   doesn't mirror as exactly onto another cell's centre the way a
   single pixel does) and ONLY through the normal progressive path -
   DrawFractalDirectly()'s low-memory fallback skips it entirely,
   deliberately: that path exists for when memory is already tight, and
   adding another allocation attempt there works against the exact
   problem it exists to work around. Coarse passes aren't folded either -
   already cheap relative to the finest pass (see kBlocksPerIdleSlice's
   own comment on where render time actually goes), so the added
   bookkeeping isn't worth it there.

   y==0 is excluded from the fold: its own mirror, windowHeight, is one
   past the last valid row, so there is no in-bounds partner to share
   the work with - it always samples directly. This costs one row's
   worth of pixels out of the whole image, not worth complicating the
   indexing scheme to reclaim. */
static unsigned char	*gSymmetryCache = NULL;
static short			gSymmetryCacheRows = 0;
#define kSymmetryCacheEmpty	255		/* outside 0..kShadingScale, so it's unambiguous as "not yet computed" - see mwFractalMath.h for kShadingScale itself */

/* SymmetryFoldEligible()
   True only when BOTH the current type's own symmetryKind and the
   current view actually line up: a type flagged kFractalSymmetryRealAxis
   only actually has mirror PAIRS to reuse when gView.centreIm is
   exactly 0.0, which is what puts the sampled grid's own row windowHeight/2
   exactly on the real axis (see MapPixelToPlaneDouble()'s own
   derivation in mwFractalMath.c) - away from that, the view simply
   doesn't sample any conjugate pairs at all, symmetric set or not. */
static Boolean SymmetryFoldEligible(void) {
	const FractalTypeDescriptor *descriptor = DescriptorForWidth(width);
	
	return descriptor != NULL
			&& descriptor->symmetryKind == kFractalSymmetryRealAxis
			&& gView.centreIm == 0.0;
}

/* AllocateSymmetryCache()/FreeSymmetryCache()
   One byte per (x, canonical-row) slot, allocated fresh for whichever
   render is about to start and freed the moment it ends or aborts -
   see EndRendering(). Sized windowWidth * (windowHeight/2 + 1): every
   pair's canonical row (min(y, windowHeight-y)) falls within
   0..windowHeight/2 inclusive, so this covers every row that could
   ever actually be looked up, with a little slack rather than an
   exact-fit calculation that would need re-deriving carefully if the
   indexing scheme here ever changes. A failed allocation just leaves
   the fold disabled for this render (gSymmetryCache stays NULL,
   SampleWithSymmetryFold() falls back to sampling directly every
   time) - correct, just not optimised, exactly how this project
   already treats AllocateOffscreenStore() failing.
   
   FreeSymmetryCache() defined first, deliberately: AllocateSymmetryCache()
   calls it defensively (see its own comment below), and a call before
   the callee's own definition has been seen forces an implicit
   declaration that then conflicts with the real static void
   definition appearing later - real testing (an actual compile) is
   what caught this, the same class of ordering mistake as
   mwFractalMath.c's kFractalTypes[] forward-declaration issue earlier
   in this project, just surfacing as a different diagnostic
   ("invalid redeclaration" here, "no such member" there) because a
   missing function declaration and an incomplete struct type fail
   differently, not because the underlying mistake is actually
   different in kind. */
static void FreeSymmetryCache(void) {
	if (gSymmetryCache != NULL) {
		DisposePtr((Ptr) gSymmetryCache);
		gSymmetryCache = NULL;
	}
	gSymmetryCacheRows = 0;
}

static void AllocateSymmetryCache(void) {
	long size = (long) windowWidth * (windowHeight / 2 + 1);
	
	/* Defensive: frees any cache already sitting in gSymmetryCache
	   before allocating a fresh one, rather than assuming
	   EndRendering() always ran first - costs nothing when it's
	   already NULL (FreeSymmetryCache()'s own no-op case), and avoids
	   a leak if that assumption is ever wrong. */
	FreeSymmetryCache();
	
	gSymmetryCache = (unsigned char *) NewPtr(size);
	
	if (gSymmetryCache != NULL) {
		gSymmetryCacheRows = windowHeight / 2 + 1;
		/* memset(), not a Toolbox call: this is filling plain,
		   already-owned heap memory with a single repeated byte, the
		   textbook case for the ANSI library's own routine - this
		   project already links it (strcpy()/strcmp(), mwSaveAs.c and
		   elsewhere), so there's no new dependency being introduced. */
		memset(gSymmetryCache, kSymmetryCacheEmpty, size);
	}
}

/* SampleWithSymmetryFold() itself is defined further down, right after
   fractalRenderJob's own declaration - it reads fractalRenderJob.sampleProc
   directly, which isn't declared until then; real testing (another
   actual compile) caught this exact same class of ordering mistake a
   second time, this time against a plain struct variable rather than
   a function or a type. */

/* Progressive render job -------------------------------------------
   Tracks an in-progress coarse-to-fine render so AdvanceFractalRender()
   can pick up where it left off each time it's called. There is only
   ever one job at a time; starting a new one (RenderFractalOffscreen())
   simply overwrites whatever was in progress.
   
   Breadth-first across the whole image at every pass: every block at
   the current size gets shaded before any of them subdivides further
   - so the entire picture refines together, coming into focus as a
   whole, rather than one region reaching full detail before the rest
   are touched.
   
   nextBlockIndex is a linear count (0 to columnCount*rowCount-1)
   rather than a (column,row) pair - MapIndexToQuadrantOrder() turns it
   into an actual grid position each time, in a recursively-quadrant-
   grouped order rather than row-major. A plain row-major sweep looks
   fine at coarse block counts (few enough blocks that a whole pass
   finishes within one or two screen updates, so the order isn't
   visible at all), but once a pass has enough blocks to take many
   visible ticks, row-major becomes a visible left-to-right,
   top-to-bottom scan - "line by line" - rather than looking like
   quadrants filling in. long, not short: at the finest colour pass
   this can run up to width*height (up to 153600 for this project's
   512x300 image), which overflows a 16-bit short. */
static struct {
	Boolean				active;
	FractalSampleProc	sampleProc;
	short				blockSize;
	short				columnCount;
	short				rowCount;
	long				nextBlockIndex;
	unsigned long		startTick;
	unsigned long		endTick;
} fractalRenderJob;

/* SampleWithSymmetryFold()
   See the "Real-axis mirror symmetry" section's own opening comment,
   above SampleWithSymmetryFold()'s original home earlier in this file,
   for the full mathematical picture - only the definition itself
   moved down here, where fractalRenderJob (just above) is actually
   visible; the reasoning above it didn't need to move with it. Called
   from DrawNextBlockAndAdvance() in place of calling
   fractalRenderJob.sampleProc() directly - falls straight through to
   it, unchanged, whenever the fold doesn't apply (wrong type, wrong
   view, cache never allocated, or y==0), so every render that doesn't
   qualify behaves exactly as it did before this existed. */
static short SampleWithSymmetryFold(short x, short y) {
	short	mirrorY, canonicalRow;
	long	cacheIndex;
	short	shadeLevel;
	
	if (gSymmetryCache == NULL || y == 0)
		return fractalRenderJob.sampleProc(x, y);
	
	mirrorY      = windowHeight - y;
	canonicalRow = (y < mirrorY) ? y : mirrorY;
	
	if (canonicalRow >= gSymmetryCacheRows)
		return fractalRenderJob.sampleProc(x, y);		/* defensive only - shouldn't happen given AllocateSymmetryCache()'s own sizing, but a stale cache from a resize mid-render must never be read out of bounds */
	
	/* (long) on canonicalRow forces the whole multiply into 32-bit
	   arithmetic before x is added - without it, canonicalRow*windowWidth
	   alone already overflows a 16-bit signed short well within this
	   project's own default 512x300 window (150*512 = 76800), not just
	   at some unusually large resize - the same reasoning
	   AllocateSymmetryCache()'s own size calculation already applies to
	   itself, missed here on the first pass through this function and
	   caught only by working the actual numbers, not by inspection. */
	cacheIndex = x + (long) canonicalRow * windowWidth;
	shadeLevel = gSymmetryCache[cacheIndex];
	
	if (shadeLevel != kSymmetryCacheEmpty)
		return shadeLevel;
	
	shadeLevel = fractalRenderJob.sampleProc(x, y);
	gSymmetryCache[cacheIndex] = (unsigned char) shadeLevel;
	
	return shadeLevel;
}

/* Blit throttling state - see kBlitIntervalTicks' own comment.
   Accumulates across possibly several AdvanceFractalRender() calls
   until it's actually time to blit, rather than growing and shrinking
   within a single call the way the job's own per-call changedRect
   does. Reset (haveAccumulatedChanges cleared) whenever a render
   starts - see BeginRendering() - since a fresh render's own initial
   erase already invalidates any region a previous, now-superseded
   render might have left pending. */
static Rect		accumulatedChangedRect;
static Boolean	haveAccumulatedChanges = false;
static unsigned long lastBlitTick = 0;

static void			BeginRendering(void);
static void			EndRendering(void);
static RGBColor		ColourForShadeLevel(short shadeLevel);
static unsigned short	InterpolateComponent(unsigned short from, unsigned short to, double fraction);
static CTabHandle	BuildFractalColourTable(void);
static short		CurrentFinestBlockSize(void);
static Boolean		ShouldRenderInColour(void);
static void			ShadeBlock(const Rect *blockRect, short shadeLevel);
static void			FillIndexedRect(const Rect *blockRect, short shadeLevel);
static short		MonoBandIndexForShadeLevel(short shadeLevel, short phase);
static void			FillMonoBand(const Rect *blockRect, short bandIndex);
static void			RecordMonoShadeLevels(const Rect *blockRect, short shadeLevel);
static void			DrawFractalDirectly(void);
static short		BlocksAcross(short span, short blockSize);
static short		HighestPowerOfTwoAtMost(short n);
static void			MapIndexToQuadrantOrder(long index, short left, short top, short width, short height, short *outColumn, short *outRow);
static Boolean		AllocateOffscreenMonoStore(void);
static Boolean		AllocateOffscreenColourStore(void);
static Boolean		AllocateOffscreenStore(void);
static void			DisposeOffscreenStore(void);
static void			StartProgressiveRender(FractalSampleProc sampleProc);
static void			UpdateIterationCeilingForBlockSize(short blockSize);
static void			DrawNextBlockAndAdvance(Rect *drawnRect);
static void			AdvanceToNextBlock(void);
static void			BeginNextPass(void);
static void			EnterOffscreenPort(void);
static void			EnterWindowPort(void);
static void			BlitOffscreenToWindow(const Rect *changedRect);
static void			DrawBranchDirectly(float x1, float y1, float angle, float depth);
static void			DrawIndexedLine(short x1, short y1, short x2, short y2, short colourIndex);
static short		BranchColourIndexForDepth(short depth);
static short		RecursiveFractalBackgroundIndex(void);

/* SetUpWindow()
   Create the Minimum Window window, and open it - a colour window via
   NewCWindow() when Color QuickDraw is present, a plain monochrome one
   via NewWindow() otherwise. Either way it's stored in the same
   WindowPtr: CWindowRecord begins with a WindowRecord, so everything
   elsewhere that reads windowKind/visible/portRect through mwWindow
   (including all of mwMenus.c) works unchanged regardless of which
   kind this actually is. */
void SetUpWindow(void) {
    dragRect = screenBits.bounds;
    
    if (gHasColourQD)
        mwWindow = NewCWindow(0L, &windowBounds, kIdleWindowTitle, true, documentProc, (WindowPtr) -1L, true, 0);
    else
        mwWindow = NewWindow(0L, &windowBounds, kIdleWindowTitle, true, documentProc, (WindowPtr) -1L, true, 0);
    
    SetPort(mwWindow);
    
    RenderFractalOffscreen();
}

/* The Tree's fixed recursion depth, passed to the initial DrawBranch()/
   DrawBranchDirectly() call at both call sites (RenderFractalOffscreen(),
   DrawFractalDirectly()) - never actually varies, so it's a constant
   rather than a parameter threaded through the recursion. Also drives
   BranchColourIndexForDepth()'s base-to-tip colour mapping: depth
   kTreeInitialDepth is the trunk (drawn first), depth 1 is the last
   segment actually drawn before the depth-0 base case ends that
   branch (the closest thing to a "tip" this recursion reaches). */
#define kTreeInitialDepth	9

/* DrawBranch()
   Recursively draws the Tree's branches into the offscreen store for
   the normal render path (RenderFractalOffscreen()) - the low-memory
   DrawFractalDirectly() fallback, which has no offscreen store to
   write into, uses DrawBranchDirectly() below instead.
   
   In colour, writes each segment via DrawIndexedLine() - a plain
   pixel-by-pixel write into the offscreen store's own memory - coloured
   by BranchColourIndexForDepth(), rather than through LineTo()/
   ForeColor(): QuickDraw's own colour-setting calls are exactly what
   ShadeBlock()'s own comment (and FillIndexedRect(), which takes the
   same direct-write approach for rects) already found unreliable on
   this offscreen GWorld, for the same RGBForeColor()/PmForeColor()
   reasons documented there. In monochrome, keeps the original
   MoveTo()/Line() drawing unchanged - there's no palette to colour by
   in monochrome, so there's nothing this change needs to do there. */
void DrawBranch(float x1, float y1, float angle, float depth) {
	if (depth != 0) {
		float x2 = x1 + cos(angle*(pi/180.0))*depth*10;
		float y2 = y1 + sin(angle*(pi/180.0))*depth*10;
		
		if (gHasColourQD) {
			short colourIndex = BranchColourIndexForDepth((short) depth);
			DrawIndexedLine((short) x1, (short) (windowHeight - y1), (short) x2, (short) (windowHeight - y2), colourIndex);
		} else {
			MoveTo(x1,windowHeight-y1);
			Line(x2-x1,y1-y2);
		}
		
		DrawBranch(x2,y2,angle-20,depth-1);
		DrawBranch(x2,y2,angle+20,depth-1);
	}
}

/* DrawBranchDirectly()
   The Tree's original drawing, unchanged: plain MoveTo()/Line() calls
   into whatever the current port is. Used only by DrawFractalDirectly()'s
   low-memory fallback, which draws straight into the window because
   there's no offscreen store available to hold a palette-indexed
   image at all - DrawBranch()'s direct-write approach above has
   nothing to write into in that situation. */
static void DrawBranchDirectly(float x1, float y1, float angle, float depth) {
	if (depth != 0) {
		float x2 = x1 + cos(angle*(pi/180.0))*depth*10;
		float y2 = y1 + sin(angle*(pi/180.0))*depth*10;
		
		MoveTo(x1,windowHeight-y1);
		Line(x2-x1,y1-y2);
		
		DrawBranchDirectly(x2,y2,angle-20,depth-1);
		DrawBranchDirectly(x2,y2,angle+20,depth-1);
	}
}

/* DrawTreeOffscreen()/DrawTreeDirectly()
   Zero-argument wrappers around DrawBranch()/DrawBranchDirectly() - see
   FractalDirectDrawProc's own comment on why kFractalTypes[] (this
   file) needs this shape rather than calling either directly. */
static void DrawTreeOffscreen(void) {
	DrawBranch(windowWidth/2, 0, 90, kTreeInitialDepth);
}

static void DrawTreeDirectly(void) {
	DrawBranchDirectly(windowWidth/2, 0, 90, kTreeInitialDepth);
}

/* RandomUnitInterval()
   A pseudo-random double in [0,1), from the Toolbox's own Random() - a
   signed 16-bit value, widened to unsigned first so the full 16-bit
   range maps onto [0,1) rather than folding the negative half back
   over the positive one. Used by the IFS/chaos-game fractals below
   (Fern, Sierpinski) to pick which transformation applies at each
   step - the only place in this project that needs a general-purpose
   random number at all, so there's no existing convention here to
   match beyond the Toolbox's own standard call. */
static double RandomUnitInterval(void) {
	return ((double) ((unsigned short) Random())) / 65536.0;
}

/* Barnsley's fern - four affine transformations applied with fixed
   probabilities (Barnsley, "Fractals Everywhere"; coefficients and
   probabilities as widely published and cross-checked against several
   independent sources before use here): f1 (p=0.01, the stem), f2
   (p=0.85, successively smaller leaflets - by far the most common,
   which is what actually builds up the frond's overall shape), f3/f4
   (p=0.07 each, the largest left/right leaflets). The fern's own
   natural coordinate range is x in [-2.5,2.5], y in [0,10] (stem at
   y=0, frond tip near y=10) - kFernPointCount points are plotted after
   discarding a short settling-in period so the arbitrary (0,0)
   starting point doesn't leave a stray mark outside the actual
   attractor.

   kFernPointCount is a deliberate compromise: enough points for a
   recognisable, reasonably detailed frond (many published renderings
   use 50,000-100,000+), but not so many that plotting them one at a
   time - the only way to draw an IFS fractal, unlike the escape-time
   family's block-filling - takes an excessive amount of time on this
   project's own slowest real target (the Mac 512KE); each point costs
   only a handful of multiplies and a single-pixel draw, considerably
   cheaper per-point than any escape-time or convergence fractal's own
   per-pixel cost, but there is still no way to draw fewer than
   kFernPointCount actual points and have all of them show up. Worth
   revisiting with real timing once this can actually be tested on
   that hardware.

   Colour follows the same "distance from the base" idea
   BranchColourIndexForDepth() already uses for the Tree - here, a
   point's own fern-space y (height above the stem) mapped directly
   onto the shared 0..kShadingScale range, rather than recursion depth,
   since an IFS fractal built by the chaos-game method has no
   recursion depth to speak of. */
#define kFernPointCount				20000
#define kFernSettlingIterations		20
#define kFernMinX					(-2.5)
#define kFernMaxX					2.5
#define kFernMinY					0.0
#define kFernMaxY					10.0
#define kFernBottomMarginPx			10

static void FernToScreen(double fernX, double fernY, short *outX, short *outY) {
	double widthScale  = windowWidth  / (kFernMaxX - kFernMinX);
	double heightScale = windowHeight / (kFernMaxY - kFernMinY);
	double scale        = 0.9 * ((widthScale < heightScale) ? widthScale : heightScale);
	
	*outX = (short) (windowWidth / 2 + fernX * scale);
	*outY = (short) (windowHeight - kFernBottomMarginPx - fernY * scale);
}

static short FernShadeLevel(double fernY) {
	double normalised = (fernY - kFernMinY) / (kFernMaxY - kFernMinY);
	
	if (normalised < 0.0)
		normalised = 0.0;
	else if (normalised > 1.0)
		normalised = 1.0;
	
	return (short) (normalised * kShadingScale);
}

static void DrawFernPoints(Boolean intoOffscreen) {
	double	x = 0.0, y = 0.0;
	long	i;
	
	for (i = 0; i < kFernSettlingIterations + kFernPointCount; i++) {
		double	roll = RandomUnitInterval();
		double	newX, newY;
		
		if (roll < 0.01) {
			newX = 0.0;
			newY = 0.16 * y;
		} else if (roll < 0.86) {
			newX = 0.85 * x + 0.04 * y;
			newY = -0.04 * x + 0.85 * y + 1.6;
		} else if (roll < 0.93) {
			newX = 0.20 * x - 0.26 * y;
			newY = 0.23 * x + 0.22 * y + 1.6;
		} else {
			newX = -0.15 * x + 0.28 * y;
			newY = 0.26 * x + 0.24 * y + 0.44;
		}
		
		x = newX;
		y = newY;
		
		if (i >= kFernSettlingIterations) {
			short screenX, screenY;
			
			FernToScreen(x, y, &screenX, &screenY);
			
			if (intoOffscreen && gHasColourQD) {
				DrawIndexedLine(screenX, screenY, screenX, screenY, FernShadeLevel(y));
			} else {
				MoveTo(screenX, screenY);
				Line(0, 0);
			}
		}
	}
}

static void DrawFernOffscreen(void) {
	DrawFernPoints(true);
}

static void DrawFernDirectly(void) {
	DrawFernPoints(false);
}

/* Sierpinski's triangle, via the chaos game (not recursive subdivision):
   start at one vertex of a fixed triangle, repeatedly move halfway
   toward a randomly-chosen vertex (any of the three, equal
   probability), plotting the new point each time - the standard,
   widely-documented construction. Vertices are placed directly in
   screen space (scaled to the current window, with a fixed margin),
   not through a separate coordinate system and mapping function the
   way the Fern's own biologically-calibrated coefficients need -
   Sierpinski's triangle has no inherent "natural" bounding box the
   way the Fern does, so there's nothing a separate space would add
   here.

   Colour: a genuine chaos-game run has no recursion depth the way
   Tree's own recursive drawing does, but choosing the SAME vertex
   several times in a row is the chaos-game's own analogue of it - a
   point that does is moving deep into that one vertex's smallest
   self-similar sub-triangle, exactly what recursing toward a corner
   would do directly. gSierpinskiRunLength (local to DrawSierpinskiPoints(),
   not persisted between calls) counts consecutive same-vertex picks,
   capped at kSierpinskiMaxRunLength for shading purposes - long runs
   are real but increasingly rare (probability (1/3)^n), so capping
   avoids most of the shading range going unused waiting for runs that
   essentially never happen at this point count. */
#define kSierpinskiPointCount			20000
#define kSierpinskiSettlingIterations	20
#define kSierpinskiMarginPx				20
#define kSierpinskiMaxRunLength			8

static void DrawSierpinskiPoints(Boolean intoOffscreen) {
	double	vertexX[3], vertexY[3];
	double	x, y;
	short	lastVertex = -1;
	short	runLength = 0;
	long	i;
	
	vertexX[0] = windowWidth / 2.0;				vertexY[0] = kSierpinskiMarginPx;
	vertexX[1] = kSierpinskiMarginPx;				vertexY[1] = windowHeight - kSierpinskiMarginPx;
	vertexX[2] = windowWidth - kSierpinskiMarginPx;	vertexY[2] = windowHeight - kSierpinskiMarginPx;
	
	x = vertexX[0];
	y = vertexY[0];
	
	for (i = 0; i < kSierpinskiSettlingIterations + kSierpinskiPointCount; i++) {
		short chosen = (short) (RandomUnitInterval() * 3.0);
		
		if (chosen > 2)
			chosen = 2;		/* guards the extremely rare RandomUnitInterval()==1.0 edge, which would otherwise index one past vertexX/Y */
		
		if (chosen == lastVertex) {
			if (runLength < kSierpinskiMaxRunLength)
				runLength++;
		} else {
			runLength = 1;
		}
		lastVertex = chosen;
		
		x = (x + vertexX[chosen]) / 2.0;
		y = (y + vertexY[chosen]) / 2.0;
		
		if (i >= kSierpinskiSettlingIterations) {
			short screenX = (short) x;
			short screenY = (short) y;
			
			if (intoOffscreen && gHasColourQD) {
				short shadeLevel = (short) (((double) runLength / (double) kSierpinskiMaxRunLength) * kShadingScale);
				DrawIndexedLine(screenX, screenY, screenX, screenY, shadeLevel);
			} else {
				MoveTo(screenX, screenY);
				Line(0, 0);
			}
		}
	}
}

static void DrawSierpinskiOffscreen(void) {
	DrawSierpinskiPoints(true);
}

static void DrawSierpinskiDirectly(void) {
	DrawSierpinskiPoints(false);
}

/* SampleMandelbrot()/SampleJulia()
   Map (x,y) through gRenderMappingDouble/Fixed (see PrepareRenderMapping()),
   dispatching on gHasFPU exactly as the maths itself does - the
   non-FPU path stays free of floating point from the pixel
   coordinates onward, not just in the iteration loop. Mandelbrot
   tests c = the mapped point (z starts at 0, and skips iteration
   entirely when IsInMainCardioidOrBulb*() already proves it interior);
   Julia iterates the mapped point as z against its own fixed c.

   Both run against currentIterationCeiling - the cheaper, reduced
   budget a coarse preview pass uses - but always shade against the
   fractal's real maxIterations, not that reduced value: normalising
   against whatever ceiling actually ran was tried first, and produced
   visibly different colours pass to pass for the same point (log-
   scaling the same count against a ceiling of 8 versus 64 gives very
   different results); the true ceiling keeps colours stable as a
   render refines. */
static short SampleMandelbrot(short x, short y) {
	short	iterationCount;
	
	if (gHasFPU) {
		double	dRe, dIm;
		
		MapPixelToPlaneDouble(&gRenderMappingDouble, x, y, &dRe, &dIm);
		
		if (IsInMainCardioidOrBulb(dRe, dIm))
			iterationCount = currentIterationCeiling;
		else
			iterationCount = IterateEscapeTimeDouble(0.0, 0.0, dRe, dIm, currentIterationCeiling);
	} else {
		Fixed	fRe, fIm;
		
		MapPixelToPlaneFixed(&gRenderMappingFixed, x, y, &fRe, &fIm);
		
		if (IsInMainCardioidOrBulbFixed(fRe, fIm))
			iterationCount = currentIterationCeiling;
		else
			iterationCount = IterateEscapeTimeFixed(0, 0, fRe, fIm, currentIterationCeiling);
	}
	
	return ShadeLevelForIterationCount(iterationCount, kMandelbrotMaxIterations);
}

static short SampleJulia(short x, short y) {
	short	iterationCount;
	
	if (gHasFPU) {
		double	dRe, dIm;
		
		MapPixelToPlaneDouble(&gRenderMappingDouble, x, y, &dRe, &dIm);
		iterationCount = IterateEscapeTimeDouble(dRe, dIm, kJuliaConstantRe, kJuliaConstantIm, currentIterationCeiling);
	} else {
		Fixed	fRe, fIm;
		
		MapPixelToPlaneFixed(&gRenderMappingFixed, x, y, &fRe, &fIm);
		iterationCount = IterateEscapeTimeFixed(fRe, fIm, kJuliaConstantReFixed, kJuliaConstantImFixed, currentIterationCeiling);
	}
	
	return ShadeLevelForIterationCount(iterationCount, kJuliaMaxIterations);
}

/* SampleBurningShip()/SampleTricorn()/SampleMultibrotConfigurable()
   All three are Mandelbrot-shaped, not Julia-shaped, like
   SampleMandelbrot() above: c is the mapped point, z starts at 0.
   None of them call IsInMainCardioidOrBulb()/Fixed() the way
   SampleMandelbrot() does - see mwFractalMath.h's own comment on
   IterateBurningShipDouble()/IterateTricornDouble() and
   IterateMultibrotDouble() for why those two closed-form tests
   describe the plain Mandelbrot set specifically and don't apply to
   any of these differently-shaped sets. All three otherwise share
   SampleMandelbrot()'s structure exactly, just calling a different
   Iterate*() pair and shading against kMandelbrotMaxIterations, which
   kFractalTypes[] (this file) uses as every one of these three types'
   own ceiling too. */
static short SampleBurningShip(short x, short y) {
	short	iterationCount;
	
	if (gHasFPU) {
		double	dRe, dIm;
		
		MapPixelToPlaneDouble(&gRenderMappingDouble, x, y, &dRe, &dIm);
		iterationCount = IterateBurningShipDouble(0.0, 0.0, dRe, dIm, currentIterationCeiling);
	} else {
		Fixed	fRe, fIm;
		
		MapPixelToPlaneFixed(&gRenderMappingFixed, x, y, &fRe, &fIm);
		iterationCount = IterateBurningShipFixed(0, 0, fRe, fIm, currentIterationCeiling);
	}
	
	return ShadeLevelForIterationCount(iterationCount, kMandelbrotMaxIterations);
}

static short SampleTricorn(short x, short y) {
	short	iterationCount;
	
	if (gHasFPU) {
		double	dRe, dIm;
		
		MapPixelToPlaneDouble(&gRenderMappingDouble, x, y, &dRe, &dIm);
		iterationCount = IterateTricornDouble(0.0, 0.0, dRe, dIm, currentIterationCeiling);
	} else {
		Fixed	fRe, fIm;
		
		MapPixelToPlaneFixed(&gRenderMappingFixed, x, y, &fRe, &fIm);
		iterationCount = IterateTricornFixed(0, 0, fRe, fIm, currentIterationCeiling);
	}
	
	return ShadeLevelForIterationCount(iterationCount, kMandelbrotMaxIterations);
}

/* gMultibrotPower/ConfigureMultibrot()
   gMultibrotPower is the power Multibrot last rendered at (default 3,
   a reasonable first look - see FractalParameters.md's own guidance),
   read by SampleMultibrotConfigurable() below every time it samples a
   pixel. ConfigureMultibrot() is Multibrot's own FractalConfigureProc
   (see kFractalTypes[]) - shown via ShowParameterDialog()
   (mwParameterDialog.h) whenever the person picks Multibrot from the
   Fractal menu, whether or not it was already the current type, so
   reselecting it is how the power gets changed, not a separate menu
   item of its own. 2..8 as the allowed range: 2 is just plain
   Mandelbrot (allowed rather than special-cased away - no real reason
   to forbid it, and Multibrot's own iteration functions handle it
   correctly regardless), and above 8 the per-iteration cost - one more
   full complex multiply per unit of power, on top of everything else -
   climbs fast for a diminishing visual return; see
   FractalParameters.md for what different values actually look like. */
static long gMultibrotPower = 3;

static Boolean ConfigureMultibrot(void) {
	ParameterField	field;
	
	field.kind    = kParameterFieldInteger;
	field.label   = "Power (n):";
	field.value   = gMultibrotPower;
	field.minimum = 2;
	field.maximum = 8;
	
	if (!ShowParameterDialog("Choose the power for z^n + c. See FractalParameters.md for what different values look like.", &field, 1))
		return false;
	
	gMultibrotPower = field.value;
	return true;
}

/* GetMultibrotPower()/SetMultibrotPower()
   See mwWindow.h. SetMultibrotPower() clamps rather than fully
   validating the way ConfigureMultibrot()'s dialog does - mwSaveAs.c's
   own load path is the only caller, and a hand-edited or corrupted
   .frct file with a nonsensical Power: value should still render
   *something* rather than fail to load at all. The clamp is wider
   than the dialog's own 2..8 (deliberately - someone hand-editing a
   file already knows they're past the suggested range) but still
   real: IterateMultibrotDouble()/Fixed() themselves stay correct for
   any power>=2, but each unit of power is one more full complex
   multiply every iteration, every pixel - an unclamped, absurdly
   large value from a corrupted file wouldn't crash, it would just make
   the app appear to hang for a very long time. */
#define kMultibrotMinimumPower	2
#define kMultibrotMaximumPower	64

long GetMultibrotPower(void) {
	return gMultibrotPower;
}

void SetMultibrotPower(long power) {
	if (power < kMultibrotMinimumPower)
		power = kMultibrotMinimumPower;
	else if (power > kMultibrotMaximumPower)
		power = kMultibrotMaximumPower;
	
	gMultibrotPower = power;
}

static short SampleMultibrotConfigurable(short x, short y) {
	short	iterationCount;
	
	if (gHasFPU) {
		double	dRe, dIm;
		
		MapPixelToPlaneDouble(&gRenderMappingDouble, x, y, &dRe, &dIm);
		iterationCount = IterateMultibrotDouble(0.0, 0.0, dRe, dIm, (short) gMultibrotPower, currentIterationCeiling);
	} else {
		Fixed	fRe, fIm;
		
		MapPixelToPlaneFixed(&gRenderMappingFixed, x, y, &fRe, &fIm);
		iterationCount = IterateMultibrotFixed(0, 0, fRe, fIm, (short) gMultibrotPower, currentIterationCeiling);
	}
	
	return ShadeLevelForIterationCount(iterationCount, kMandelbrotMaxIterations);
}

static short SamplePhoenix(short x, short y) {
	short	iterationCount;
	
	if (gHasFPU) {
		double	dRe, dIm;
		
		MapPixelToPlaneDouble(&gRenderMappingDouble, x, y, &dRe, &dIm);
		iterationCount = IteratePhoenixDouble(0.0, 0.0, dRe, dIm, currentIterationCeiling);
	} else {
		Fixed	fRe, fIm;
		
		MapPixelToPlaneFixed(&gRenderMappingFixed, x, y, &fRe, &fIm);
		iterationCount = IteratePhoenixFixed(0, 0, fRe, fIm, currentIterationCeiling);
	}
	
	return ShadeLevelForIterationCount(iterationCount, kMandelbrotMaxIterations);
}

/* gLyapunovSequence/gLyapunovSequenceLength/ConfigureLyapunov()
   The driving sequence Lyapunov last rendered with (default "AB", the
   single most commonly shown Lyapunov fractal sequence in the
   literature - see FractalParameters.md), and Lyapunov's own
   FractalConfigureProc. Unlike Multibrot's power, this needs its own,
   fractal-specific validation after ShowParameterDialog() returns -
   the generic dialog only guarantees non-empty text (see
   mwParameterDialog.h's own comment on why), so this checks every
   character is 'A' or 'B' itself, re-showing the dialog with a more
   specific prompt if not, rather than accepting whatever was typed.
   Normalises to uppercase on the way in, so "ab" and "AB" are treated
   the same and always displayed the same way back afterwards. */
static char  gLyapunovSequence[64] = "AB";
static short gLyapunovSequenceLength = 2;

static Boolean ConfigureLyapunov(void) {
	ParameterField	field;
	const char		*prompt = "Enter a sequence of A's and B's (e.g. AB, AABAB). See FractalParameters.md for what different sequences look like.";
	
	field.kind = kParameterFieldText;
	field.label = "Sequence (A/B):";
	strcpy(field.text, gLyapunovSequence);
	
	for (;;) {
		short	i;
		short	length;
		Boolean	valid = true;
		
		if (!ShowParameterDialog(prompt, &field, 1))
			return false;
		
		length = (short) strlen(field.text);
		if (length == 0)
			valid = false;
		
		for (i = 0; i < length; i++) {
			char c = field.text[i];
			if (c >= 'a' && c <= 'z')
				c -= 32;		/* uppercase - matches this fractal's own display/storage convention */
			if (c != 'A' && c != 'B') {
				valid = false;
				break;
			}
			field.text[i] = c;
		}
		
		if (valid) {
			strcpy(gLyapunovSequence, field.text);
			gLyapunovSequenceLength = length;
			return true;
		}
		
		prompt = "Only the letters A and B are allowed - try again (e.g. AB, AABAB).";
	}
}

/* SampleLyapunov()
   The one fractal type in this project that isn't escape-time,
   convergence, or direct-draw at all: a and b - not c, not z - are
   what the view actually describes here, reusing gRenderMappingDouble/
   MapPixelToPlaneDouble() exactly as every other fractal does for its
   own Re/Im, since the underlying pixel-to-plane mapping is the same
   linear transform regardless of what the two numbers it produces are
   then used for. Always double (see IterateLyapunovExponent()'s own
   comment) - reads gRenderMappingDouble regardless of gHasFPU, which
   PrepareRenderMapping() always prepares for exactly this reason.
   
   The exponent itself is mapped onto the shared 0..kShadingScale range
   via tanh(), rather than a hard clamp: real Lyapunov fractals show a
   smooth gradient from strongly stable through borderline to strongly
   chaotic, not sharply banded regions the way Newton's discrete "which
   root" categories will - a hard clamp would show flat, saturated
   colour patches for anything beyond the clamp threshold, where tanh's
   smooth saturation keeps extreme values visually distinct without
   letting a few outliers compress the interesting, near-zero boundary
   region into a handful of shades. kLyapunovShadeScale controls how
   quickly the mapping saturates - chosen by inspection of this map's
   typical exponent range for a/b in [2.5,4.0] (a Python port of this
   exact formula was checked against several well-known reference
   points - r=2.5 stable, r=3.9 chaotic, r=3.83's well-known periodic
   window inside the chaotic region correctly stable again - before
   relying on it here), not derived from a formal bound. */
#define kLyapunovShadeScale	0.7

static short SampleLyapunov(short x, short y) {
	double	a, b;
	double	exponent;
	double	normalised;
	short	shadeLevel;
	
	MapPixelToPlaneDouble(&gRenderMappingDouble, x, y, &a, &b);
	
	exponent = IterateLyapunovExponent(a, b, gLyapunovSequence, gLyapunovSequenceLength);
	
	normalised = 0.5 + 0.5 * tanh(exponent / kLyapunovShadeScale);
	shadeLevel = (short) (normalised * kShadingScale);
	
	if (shadeLevel < 0)
		shadeLevel = 0;
	else if (shadeLevel > kShadingScale)
		shadeLevel = kShadingScale;
	
	return shadeLevel;
}

/* gNewtonPower/ConfigureNewton()
   Newton's own power - which power-th roots of unity z^power-1's
   basins are drawn around - reusing the same dialog and the same
   2..8 range as Multibrot's own power, for the same reasons given
   there: a real "enter any n" dialog rather than fixed menu entries,
   and a range wide enough for real variety without runaway per-pixel
   cost (each unit of power here costs one more complex multiply per
   Newton iteration, same as Multibrot, on top of the division every
   iteration already needs regardless of power). Default 3 - the
   single most commonly shown Newton fractal (z^3-1) in the
   literature - see FractalParameters.md. */
static long gNewtonPower = 3;

/* GetNewtonPower()/SetNewtonPower()
   See mwWindow.h. Same clamp-not-reject behaviour on load as
   SetMultibrotPower() - a corrupted or hand-edited .frct file with a
   nonsensical Power: value should still render something. The clamp's
   upper bound matters less here than for Multibrot: an unclamped huge
   power would cost one more complex multiply per Newton iteration
   (same as Multibrot), on top of the division every Newton iteration
   already needs regardless of power - the ceiling is about the same
   order of magnitude as Multibrot's own, not derived independently. */
#define kNewtonMinimumPower	2
#define kNewtonMaximumPower	64

long GetNewtonPower(void) {
	return gNewtonPower;
}

void SetNewtonPower(long power) {
	if (power < kNewtonMinimumPower)
		power = kNewtonMinimumPower;
	else if (power > kNewtonMaximumPower)
		power = kNewtonMaximumPower;
	
	gNewtonPower = power;
}

static Boolean ConfigureNewton(void) {
	ParameterField	field;
	
	field.kind    = kParameterFieldInteger;
	field.label   = "Power (n):";
	field.value   = gNewtonPower;
	field.minimum = 2;
	field.maximum = 8;
	
	if (!ShowParameterDialog("Choose the power for z^n - 1 (which roots the basins surround). See FractalParameters.md for what different values look like.", &field, 1))
		return false;
	
	gNewtonPower = field.value;
	return true;
}

/* SampleNewton()
   Not escape-time, statistical, or direct-draw: convergence toward one
   of z^power-1's own power roots (IterateNewton(), mwNewtonMath.c).
   Always double, for the same reason SampleLyapunov() is (see
   IterateNewton()'s own comment on Fixed-point division) - reads
   gRenderMappingDouble regardless of gHasFPU, exactly as Lyapunov does.
   
   Colour encodes two things at once through the single shared
   0..kShadingScale range every fractal in this project reports on:
   which root (a discrete category, 0..power-1) as a band of the full
   range, and how fast this pixel converged (a continuous value) as
   the shade within that root's own band - the same "band per discrete
   category, shade within it for a continuous one" idea
   IsInMainCardioidOrBulb()-style closed-form shortcuts don't need but
   a convergence fractal's very different colouring problem does. A
   point that never converges within kNewtonMaxIterations (effectively
   only ever the z=0 critical point itself, or something numerically
   indistinguishable from it) gets shade 0 rather than being assigned
   to any root's band, since it didn't actually reach one. */
static short SampleNewton(short x, short y) {
	double	startRe, startIm;
	double	finalRe, finalIm;
	short	iterationCount;
	short	power = (short) gNewtonPower;
	short	bandWidth;
	short	rootIndex;
	short	withinBand;
	
	MapPixelToPlaneDouble(&gRenderMappingDouble, x, y, &startRe, &startIm);
	
	iterationCount = IterateNewton(startRe, startIm, power, currentIterationCeiling, &finalRe, &finalIm);
	
	if (iterationCount >= currentIterationCeiling)
		return 0;
	
	bandWidth  = kShadingScale / power;
	rootIndex  = NewtonRootIndex(finalRe, finalIm, power);
	withinBand = (iterationCount < bandWidth) ? iterationCount : (short) (bandWidth - 1);
	
	return (short) (rootIndex * bandWidth + withinBand);
}

/* The colour ramp shadeLevel is mapped onto, in the same direction as
   the monochrome buckets below: 0 (fast escape) is light or dark
   depending on the palette's own aesthetic, kShadingScale (slow
   escape, or never) is the palette's other extreme. Each palette is
   its own list of colour stops, interpolated the same way regardless
   of how many stops it has - a palette with 3 stops (Greyscale) and
   one with 7 (Rainbow) are handled identically by ColourForShadeLevel().
   
   Order in kPalettes[] must match the Palette submenu's AppendMenu()
   string in mwMenus.c exactly - GetCurrentPalette()/SetCurrentPalette()
   work in terms of this array's 0-based index, which the (1-based)
   menu item number maps onto directly. */
typedef struct {
	short		shadeLevel;
	RGBColor	colour;
} ColourRampStop;

/* The offscreen colour table reserves two fixed entries beyond the
   kShadingScale+1 palette-driven shading range: a genuine white and a
   genuine black, used only as backgrounds for recursive/direct-draw
   fractals (currently just the Tree - see RecursiveFractalBackgroundIndex()
   and DrawBranch()). Neither is ever touched by a palette rebuild
   (RebuildOffscreenColourTableForCurrentPalette() only ever writes
   0..kShadingScale) or by Animate's colour-table rotation
   (mwColourCycle.c's RotateColourTable() only ever rotates that same
   range, via GetRotatableColourTableEntryCount()) - they stay a true
   white and true black regardless of which palette is active or how
   far it's been rotated, which matters for a palette like Night that
   has no true white or black stop of its own to fall back on. */
#define kBackgroundWhiteIndex	(kShadingScale + 1)
#define kBackgroundBlackIndex	(kShadingScale + 2)
#define kColourTableEntryCount	(kShadingScale + 3)

#define kMaxColourRampStops	7

typedef struct {
	const char		*name;
	short			stopCount;
	ColourRampStop	stops[kMaxColourRampStops];
} PaletteDefinition;

/* name is used two ways: mwMenus.c's Palette submenu string must list
   these in this exact same order (AppendMenu() takes one hardcoded
   Pascal string, not this array, so the two have to be kept in sync
   by hand - see the comment there), and GetPaletteName()/
   FindPaletteByName() below use it directly for saving/loading a
   palette by name in a FRCT file (see mwSaveAs.c) rather than by this
   array's index, so a saved file's meaning survives even if palettes
   are ever reordered. */
static const PaletteDefinition kPalettes[] = {
	/* Default - white through yellow/orange/red-purple to black; the
	   original ramp, unchanged from before palettes existed. */
	{ "Default", 5, {
		{ 0,                       { 65535, 65535, 65535 } },
		{ kShadingScale / 4,       { 65535, 65535, 0     } },
		{ kShadingScale / 2,       { 65535, 16384, 0     } },
		{ (kShadingScale * 3) / 4, { 32768, 0,     16384 } },
		{ kShadingScale,           { 0,     0,     0     } }
	}},
	/* Night - dark navy through indigo and deep purple to near-black. */
	{ "Night", 4, {
		{ 0,                       { 0,     0,     16384 } },
		{ kShadingScale / 3,       { 8192,  0,     32768 } },
		{ (kShadingScale * 2) / 3, { 24576, 0,     40960 } },
		{ kShadingScale,           { 4096,  0,     8192  } }
	}},
	/* Stormy - pale grey through slate grey and charcoal to near-black,
	   a cool undertone throughout. */
	{ "Stormy", 4, {
		{ 0,                       { 49152, 49152, 53248 } },
		{ kShadingScale / 3,       { 28672, 28672, 32768 } },
		{ (kShadingScale * 2) / 3, { 12288, 12288, 16384 } },
		{ kShadingScale,           { 2048,  2048,  4096  } }
	}},
	/* Summery - white through bright yellow and sky blue to grass
	   green. */
	{ "Summery", 4, {
		{ 0,                       { 65535, 65535, 65535 } },
		{ kShadingScale / 3,       { 65535, 65535, 16384 } },
		{ (kShadingScale * 2) / 3, { 16384, 49152, 65535 } },
		{ kShadingScale,           { 8192,  49152, 8192  } }
	}},
	/* Autumnal - pale gold through orange and rust red to deep brown. */
	{ "Autumnal", 4, {
		{ 0,                       { 65535, 57344, 32768 } },
		{ kShadingScale / 3,       { 65535, 32768, 8192  } },
		{ (kShadingScale * 2) / 3, { 49152, 16384, 4096  } },
		{ kShadingScale,           { 24576, 8192,  4096  } }
	}},
	/* Wintery - white through pale ice blue and pale grey to soft
	   blue-grey. */
	{ "Wintery", 4, {
		{ 0,                       { 65535, 65535, 65535 } },
		{ kShadingScale / 3,       { 53248, 60416, 65535 } },
		{ (kShadingScale * 2) / 3, { 45056, 45056, 49152 } },
		{ kShadingScale,           { 28672, 32768, 40960 } }
	}},
	/* Pastel - soft pink, lavender, mint, pale yellow, soft peach - all
	   high-lightness, low-saturation. */
	{ "Pastel", 5, {
		{ 0,                       { 65535, 53248, 57344 } },
		{ kShadingScale / 4,       { 53248, 49152, 65535 } },
		{ kShadingScale / 2,       { 49152, 65535, 57344 } },
		{ (kShadingScale * 3) / 4, { 65535, 65535, 49152 } },
		{ kShadingScale,           { 65535, 57344, 49152 } }
	}},
	/* Rainbow - a full hue sweep: red, orange, yellow, green, blue,
	   indigo, violet. */
	{ "Rainbow", 7, {
		{ 0,                       { 65535, 0,     0     } },
		{ (kShadingScale * 1) / 6, { 65535, 32768, 0     } },
		{ (kShadingScale * 2) / 6, { 65535, 65535, 0     } },
		{ (kShadingScale * 3) / 6, { 0,     65535, 0     } },
		{ (kShadingScale * 4) / 6, { 0,     0,     65535 } },
		{ (kShadingScale * 5) / 6, { 24576, 0,     65535 } },
		{ kShadingScale,           { 40960, 0,     65535 } }
	}},
	/* Fire (suggested) - white through bright yellow and orange to
	   deep red then black - hotter and more saturated than Default. */
	{ "Fire", 5, {
		{ 0,                       { 65535, 65535, 65535 } },
		{ kShadingScale / 4,       { 65535, 65535, 8192  } },
		{ kShadingScale / 2,       { 65535, 24576, 0     } },
		{ (kShadingScale * 3) / 4, { 49152, 0,     0     } },
		{ kShadingScale,           { 0,     0,     0     } }
	}},
	/* Ocean (suggested) - white through cyan and teal to deep navy. */
	{ "Ocean", 4, {
		{ 0,                       { 65535, 65535, 65535 } },
		{ kShadingScale / 3,       { 16384, 57344, 65535 } },
		{ (kShadingScale * 2) / 3, { 0,     32768, 40960 } },
		{ kShadingScale,           { 0,     4096,  16384 } }
	}},
	/* Greyscale (suggested) - pure white through mid grey to black, no
	   hue at all - a plain baseline, and cheap to reason about when
	   debugging shading itself independent of any palette's own
	   colour choices. */
	{ "Greyscale", 3, {
		{ 0,                       { 65535, 65535, 65535 } },
		{ kShadingScale / 2,       { 32768, 32768, 32768 } },
		{ kShadingScale,           { 0,     0,     0     } }
	}}
};

#define kPaletteCount ((short) (sizeof(kPalettes) / sizeof(kPalettes[0])))

/* Persists across fractal switches (not reset by
   ResetViewForCurrentFractal()) and across renders - a palette choice
   is a display preference, not part of any one fractal's own state. */
static short currentPalette = 0;

/* InterpolateComponent()
   Linear blend of one RGBColor component between two ramp stops. */
static unsigned short InterpolateComponent(unsigned short from, unsigned short to, double fraction) {
	return (unsigned short) (from + (to - from) * fraction);
}

/* ColourForShadeLevel()
   Finds the pair of the current palette's ramp stops shadeLevel falls
   between and linearly blends their colours - identical logic
   regardless of which palette is selected or how many stops it has. */
static RGBColor ColourForShadeLevel(short shadeLevel) {
	const PaletteDefinition *palette = &kPalettes[currentPalette];
	short i;
	
	for (i = 1; i < palette->stopCount; i++) {
		if (shadeLevel <= palette->stops[i].shadeLevel) {
			short  rangeStart = palette->stops[i-1].shadeLevel;
			short  rangeEnd   = palette->stops[i].shadeLevel;
			double fraction   = (rangeEnd > rangeStart) ? (double) (shadeLevel - rangeStart) / (rangeEnd - rangeStart) : 0.0;
			RGBColor result;
			
			result.red   = InterpolateComponent(palette->stops[i-1].colour.red,   palette->stops[i].colour.red,   fraction);
			result.green = InterpolateComponent(palette->stops[i-1].colour.green, palette->stops[i].colour.green, fraction);
			result.blue  = InterpolateComponent(palette->stops[i-1].colour.blue,  palette->stops[i].colour.blue,  fraction);
			return result;
		}
	}
	
	return palette->stops[palette->stopCount - 1].colour;
}

/* IsCurrentPaletteDark()
   Whether the active palette reads as predominantly dark overall -
   averages a standard perceptual luma weighting (0.30/0.59/0.11,
   scaled by 100 to stay in integer arithmetic) across all of the
   palette's own stops, compared against the midpoint of the RGB
   component range. Used to choose a contrasting background for
   recursive fractals - see RecursiveFractalBackgroundIndex() - so a
   palette like Night (all dark stops) gets a light background rather
   than another dark one on top of it, and a palette like Wintery (all
   pale stops) gets a dark one. */
static Boolean IsCurrentPaletteDark(void) {
	const PaletteDefinition *palette = &kPalettes[currentPalette];
	long total = 0;
	short i;
	
	for (i = 0; i < palette->stopCount; i++) {
		RGBColor colour = palette->stops[i].colour;
		total += ((long) colour.red * 30 + (long) colour.green * 59 + (long) colour.blue * 11) / 100;
	}
	
	return (total / palette->stopCount) < 32768;
}

/* RecursiveFractalBackgroundIndex()
   The background colour index for recursive/direct-draw fractals -
   currently just the Tree, via RenderFractalOffscreen(). Chosen for
   contrast against whichever palette is active, rather than a fixed
   colour: kBackgroundWhiteIndex if the palette reads as predominantly
   dark overall (IsCurrentPaletteDark()), kBackgroundBlackIndex if it
   reads as predominantly light. Both are the two fixed,
   palette-independent entries described in kBackgroundWhiteIndex/
   kBackgroundBlackIndex's own comment, so this is a genuine white or
   black background regardless of what colours the active palette
   itself happens to define. */
static short RecursiveFractalBackgroundIndex(void) {
	return IsCurrentPaletteDark() ? kBackgroundWhiteIndex : kBackgroundBlackIndex;
}

/* BranchColourIndexForDepth()
   Maps DrawBranch()'s current recursion depth onto a palette index
   spanning the full shading range: kTreeInitialDepth (the trunk, drawn
   first) to 0, and depth 1 (the last segment actually drawn before
   the depth-0 base case ends a branch, the closest this recursion
   gets to a "tip") to kShadingScale - so Animate's existing
   colour-table rotation (mwColourCycle.c), completely unchanged,
   already produces the requested "cycle from base to tip through the
   palette" effect once branches carry these indices: rotating the
   table shifts whichever colour was at the trunk toward the tips (or
   the reverse, depending on rotation direction), with no
   animation-specific code of its own needed here. */
static short BranchColourIndexForDepth(short depth) {
	return (short) (((kTreeInitialDepth - depth) * kShadingScale) / (kTreeInitialDepth - 1));
}

/* BuildFractalColourTable()
   Hand-builds a ColorTable of kColourTableEntryCount entries (a Handle
   sized for ColorTable's trailing variable-length ctTable array): one
   per possible shadeLevel (0..kShadingScale), so the offscreen
   GWorld's CLUT is our own fractal ramp rather than the system
   default, plus the two fixed white/black background entries - see
   kBackgroundWhiteIndex/kBackgroundBlackIndex's own comment. The
   caller owns the returned handle; NewGWorld() copies what it needs
   from it rather than keeping it, so it should be disposed (via
   DisposeCTable()) once passed to NewGWorld(). Returns NULL on low
   memory. */
static CTabHandle BuildFractalColourTable(void) {
	long		tableSize  = sizeof(ColorTable) + (long) (kColourTableEntryCount - 1) * sizeof(ColorSpec);
	CTabHandle	colourTable = (CTabHandle) NewHandle(tableSize);
	short		i;
	
	if (colourTable == NULL)
		return NULL;
	
	(**colourTable).ctSeed  = GetCTSeed();
	(**colourTable).ctFlags = 0;
	(**colourTable).ctSize  = kColourTableEntryCount - 1;
	
	for (i = 0; i <= kShadingScale; i++) {
		(**colourTable).ctTable[i].value = i;
		(**colourTable).ctTable[i].rgb   = ColourForShadeLevel(i);
	}
	
	(**colourTable).ctTable[kBackgroundWhiteIndex].value     = kBackgroundWhiteIndex;
	(**colourTable).ctTable[kBackgroundWhiteIndex].rgb.red   = 65535;
	(**colourTable).ctTable[kBackgroundWhiteIndex].rgb.green = 65535;
	(**colourTable).ctTable[kBackgroundWhiteIndex].rgb.blue  = 65535;
	
	(**colourTable).ctTable[kBackgroundBlackIndex].value     = kBackgroundBlackIndex;
	(**colourTable).ctTable[kBackgroundBlackIndex].rgb.red   = 0;
	(**colourTable).ctTable[kBackgroundBlackIndex].rgb.green = 0;
	(**colourTable).ctTable[kBackgroundBlackIndex].rgb.blue  = 0;
	
	return colourTable;
}

/* CurrentScreenDepth()
   The main screen's current pixel depth in bits (1, 2, 4, 8, 16, or
   32). Checked fresh each time rather than cached, since it's cheap
   (a couple of field reads, no searching) and it means a depth change
   made mid-session via the Monitors control panel is picked up on the
   next render rather than needing a relaunch. Assumes a single
   display, matching the simplification already made elsewhere for
   this app's fixed small window. */
short CurrentScreenDepth(void) {
	GDHandle		mainDevice       = GetMainDevice();
	PixMapHandle	mainDevicePixMap = (**mainDevice).gdPMap;
	
	return (**mainDevicePixMap).pixelSize;
}

/* ShouldRenderInColour()
   Color QuickDraw being present isn't by itself a reason to draw in
   colour: at 1-bit and 2-bit depths the render should look exactly
   like a genuine black-and-white Mac, with no attempt at colour at
   all, rather than colour that then gets dithered down to almost
   nothing meaningful. This is the single place that decision is made;
   ShadeBlock() and CurrentFinestBlockSize() both defer to it instead
   of checking gHasColourQD directly. */
static Boolean ShouldRenderInColour(void) {
	return gHasColourQD && (CurrentScreenDepth() >= 4);
}

/* CurrentFinestBlockSize()
   Colour refines all the way to real 1x1 pixels. Monochrome - which
   now includes 1-bit and 2-bit colour screens, not just genuinely
   monochrome ones, see ShouldRenderInColour() - stops one level short,
   at 2x2, leaving room for a dither pattern to simulate colour at the
   finest visible unit - the same 2x2 granularity the original
   hand-written Mandelbrot()/Julia() sampled at, now generalised to
   every block size via ShadeBlock(). */
static short CurrentFinestBlockSize(void) {
	return ShouldRenderInColour() ? 1 : 2;
}

/* ShadeBlock()
   Colours a block according to how far up the shared kShadingScale its
   sample fell. In monochrome, that's one of QuickDraw's standard
   dither patterns via FillRect() - the mechanism is unchanged from
   before this file supported colour, but the threshold values are
   evenly-spaced fifths of kShadingScale rather than the original
   uneven split. That split was tuned for a linear iteration-to-shade
   mapping; once that became the log-scale mapping in
   ShadeLevelForIterationCount(), it left "black" firing for almost
   any non-trivial iteration count (roughly shadeLevel > 32 turns out
   to correspond to a raw Mandelbrot iteration count of only about 7
   out of 64) - washing out the characteristic grey detail band into a
   solid black interior, a real regression caught on real testing.
   Evenly dividing the range instead spreads the five bands back
   across where log-scaled values actually land. This affects only
   the monochrome branch; the colour ramp below was already
   recalibrated for the log scale when that mapping was introduced.
   
   In colour it's a direct pixel-memory write via FillIndexedRect()
   rather than any QuickDraw colour-setting call: shadeLevel already
   *is* the correct index into our own colour table
   (BuildFractalColourTable() constructs it that way on purpose), so
   there's nothing to search for or match - we already know the exact
   byte we want written. This is the second colour-setting approach
   tried here. RGBForeColor() searched a colour table for the nearest
   match against whatever the *current device* happened to be, which
   without SetGWorld() correctly making our offscreen GWorld's own
   device current (removed after it crashed - see
   EnterOffscreenPort()) was very likely still the real screen's
   device regardless of its actual depth - consistent with real
   testing (fine at 8-bit, white at 1/2/4-bit, wrong at 16/32-bit).
   PmForeColor() would have sidestepped that same search, but its
   Palette Manager glue isn't linked into this project and pulling it
   in wasn't chased down. Writing the byte directly avoids both: no
   device dependency, no Palette Manager dependency, nothing to link.
   
   The mono threshold ladder itself lives in MonoBandIndexForShadeLevel()
   /FillMonoBand() now, not inline here, so ApplyMonoPatternPhase() (see
   mwColourCycle.c's Animate feature) can redraw a cell from a
   previously-recorded shade level using the same ladder, just with a
   phase added in. Recording that shade level, via
   RecordMonoShadeLevels(), runs whenever gMonoShadeLevels exists (see
   AllocateOffscreenMonoStore()) - a NULL check, essentially free, for
   the low-memory fallback path where it doesn't. */
static void ShadeBlock(const Rect *blockRect, short shadeLevel) {
	if (ShouldRenderInColour()) {
		FillIndexedRect(blockRect, shadeLevel);
		return;
	}
	
	FillMonoBand(blockRect, MonoBandIndexForShadeLevel(shadeLevel, 0));
	
	if (gMonoShadeLevels != NULL)
		RecordMonoShadeLevels(blockRect, shadeLevel);
}

/* MonoBandIndexForShadeLevel()
   Maps a shadeLevel (0..kShadingScale) to one of five band indices -
   0 (lightest) through 4 (darkest) - via the same evenly-spaced-fifths
   ladder ShadeBlock() always used, then adds phase and wraps, so
   ApplyMonoPatternPhase()'s animated redraw and this file's normal
   rendering share one definition of where the bands fall. phase is 0
   for normal rendering (see ShadeBlock()), and mwColourCycle.c's
   current animation phase otherwise. */
static short MonoBandIndexForShadeLevel(short shadeLevel, short phase) {
	short bandIndex;
	
	if (shadeLevel > (kShadingScale * 4) / 5)
		bandIndex = 4;
	else if (shadeLevel > (kShadingScale * 3) / 5)
		bandIndex = 3;
	else if (shadeLevel > (kShadingScale * 2) / 5)
		bandIndex = 2;
	else if (shadeLevel > kShadingScale / 5)
		bandIndex = 1;
	else
		bandIndex = 0;
	
	bandIndex = (bandIndex + phase) % 5;
	if (bandIndex < 0)
		bandIndex += 5;
	
	return bandIndex;
}

/* FillMonoBand()
   Fills blockRect with the one standard QuickDraw dither pattern
   corresponding to bandIndex (0 lightest/white through 4
   darkest/black) - a plain switch rather than an array of Pattern
   pointers, since Think C's handling of QuickDraw's Pattern globals in
   a static initializer isn't something worth risking untested. */
static void FillMonoBand(const Rect *blockRect, short bandIndex) {
	switch (bandIndex) {
		case 4:  FillRect(blockRect, black);  break;
		case 3:  FillRect(blockRect, dkGray); break;
		case 2:  FillRect(blockRect, gray);   break;
		case 1:  FillRect(blockRect, ltGray); break;
		default: FillRect(blockRect, white);  break;
	}
}

/* RecordMonoShadeLevels()
   Writes shadeLevel into every finest-grid cell gMonoShadeLevels has
   for the area blockRect covers - a single cell when blockRect is
   already finest-sized, or several when it's a coarser pass's block,
   in which case a later, finer pass's own call here will overwrite
   the same cells with more accurate values before a render completes.
   Clips against gMonoShadeLevelColumns/Rows defensively, in case
   blockRect's ceiling-divided grid extends slightly past the buffer's
   own bounds at the image's true edge - mirroring the same edge
   handling BlocksAcross() already requires elsewhere in this file. */
static void RecordMonoShadeLevels(const Rect *blockRect, short shadeLevel) {
	short finestSize   = CurrentFinestBlockSize();
	short startColumn  = blockRect->left / finestSize;
	short startRow     = blockRect->top  / finestSize;
	short endColumn    = (blockRect->right  + finestSize - 1) / finestSize;
	short endRow       = (blockRect->bottom + finestSize - 1) / finestSize;
	short column, row;
	
	if (endColumn > gMonoShadeLevelColumns)
		endColumn = gMonoShadeLevelColumns;
	if (endRow > gMonoShadeLevelRows)
		endRow = gMonoShadeLevelRows;
	
	for (row = startRow; row < endRow; row++) {
		unsigned char *rowStart = gMonoShadeLevels + (long) row * gMonoShadeLevelColumns + startColumn;
		
		for (column = startColumn; column < endColumn; column++)
			*rowStart++ = (unsigned char) shadeLevel;
	}
}

/* FillIndexedRect()
   Writes shadeLevel directly into every pixel byte of blockRect in
   the offscreen GWorld's own pixel memory - only ever called once
   ShouldRenderInColour() is true, so the GWorld (8 bits per pixel, one
   byte per pixel) is known to exist and be locked. rowBytes carries
   two flag bits above the actual per-row byte count for a genuine
   PixMap (the same bits CopyBits() itself relies on elsewhere in this
   file to recognise a PixMap masquerading as a BitMap), so those are
   masked off before use. */
static void FillIndexedRect(const Rect *blockRect, short shadeLevel) {
	PixMapHandle	pixMap   = ((CGrafPtr) offscreenGWorld)->portPixMap;
	Ptr				baseAddr = (**pixMap).baseAddr;
	long			rowBytes = (**pixMap).rowBytes & 0x3FFF;
	short			row, column;
	
	for (row = blockRect->top; row < blockRect->bottom; row++) {
		unsigned char *rowStart = (unsigned char *) baseAddr + (long) row * rowBytes + blockRect->left;
		
		for (column = blockRect->left; column < blockRect->right; column++)
			*rowStart++ = (unsigned char) shadeLevel;
	}
}

/* DrawIndexedLine()
   Draws a straight line by writing colourIndex bytes directly into the
   offscreen store's PixMap memory, pixel by pixel via a plain integer
   Bresenham walk - the same direct-write technique FillIndexedRect()
   uses for rects, and for the same reason: QuickDraw's own
   LineTo()/ForeColor() are what this project has already found
   unreliable for setting colour on this offscreen GWorld (see
   ShadeBlock()'s comment). Used by DrawBranch() for the Tree; nothing
   else in this project draws lines that need colour.
   
   Clips to the offscreen bounds itself, one pixel at a time, since a
   direct memory write isn't automatically clipped by QuickDraw the
   way LineTo() would be - the Tree's branches can compute endpoints
   slightly outside the image at the widest angles. */
static void DrawIndexedLine(short x1, short y1, short x2, short y2, short colourIndex) {
	PixMapHandle	pixMap   = ((CGrafPtr) offscreenGWorld)->portPixMap;
	Ptr				baseAddr = (**pixMap).baseAddr;
	long			rowBytes = (**pixMap).rowBytes & 0x3FFF;
	short			dx       = (x1 < x2) ? (x2 - x1) : (x1 - x2);
	short			dy       = (y1 < y2) ? (y2 - y1) : (y1 - y2);
	short			sx       = (x1 < x2) ? 1 : -1;
	short			sy       = (y1 < y2) ? 1 : -1;
	short			err      = dx - dy;
	
	for (;;) {
		if (x1 >= 0 && x1 < windowWidth && y1 >= 0 && y1 < windowHeight)
			*((unsigned char *) baseAddr + (long) y1 * rowBytes + x1) = (unsigned char) colourIndex;
		
		if (x1 == x2 && y1 == y2)
			break;
		
		{
			short e2 = err * 2;
			if (e2 > -dy) { err -= dy; x1 += sx; }
			if (e2 < dx)  { err += dx; y1 += sy; }
		}
	}
}

/* DrawFractalDirectly()
   Draws the whole image into the current port in one pass at full
   resolution, with no progress shown along the way. This is only used
   when there's no offscreen store to render into (see DrawContent()) -
   a rare, already-degraded situation where keeping the fallback simple
   matters more than keeping it responsive. Works in colour or
   monochrome exactly like the progressive path, since it shares
   ShadeBlock() and just steps by CurrentFinestBlockSize() instead of
   working through a job. Sets currentIterationCeiling to each
   fractal's real, full ceiling explicitly - there's no progressive
   pass here to reduce it for, and this result has to be completely
   correct in one shot since nothing will refine it further. */
static void DrawFractalDirectly(void) {
	const FractalTypeDescriptor *descriptor = DescriptorForWidth(width);
	
	EraseRect(&imageStart);
	
	if (descriptor == NULL)
		return;
	
	if (descriptor->directDrawDirectProc != NULL) {
		descriptor->directDrawDirectProc();
	} else if (descriptor->sampleProc != NULL) {
		short step = CurrentFinestBlockSize();
		short x, y;
		
		currentIterationCeiling = descriptor->maxIterations;
		PrepareRenderMapping();
		
		for (y = 0; y < windowHeight; y += step) {
			for (x = 0; x < windowWidth; x += step) {
				Rect cell;
				SetRect(&cell, x, y, x + step, y + step);
				ShadeBlock(&cell, descriptor->sampleProc(x + step/2, y + step/2));
			}
		}
	}
}

/* BlocksAcross()
   How many blockSize-wide blocks it takes to cover span, rounding up
   so a partial block at the far edge is still included. */
static short BlocksAcross(short span, short blockSize) {
	return (span + blockSize - 1) / blockSize;
}

/* HighestPowerOfTwoAtMost()
   The largest power of two that doesn't exceed n. Used to pick a
   starting block size that's clean to halve down to whichever finest
   size CurrentFinestBlockSize() reports. */
static short HighestPowerOfTwoAtMost(short n) {
	short powerOfTwo = 1;
	
	while (powerOfTwo * 2 <= n)
		powerOfTwo *= 2;
	
	return powerOfTwo;
}

/* MapIndexToQuadrantOrder()
   Turns a linear index into a (column,row) position within a
   left/top/width/height grid region, visiting cells in a recursively
   quadrant-grouped order: the whole region splits into up to four
   quadrants (top-left, top-right, bottom-left, bottom-right, splitting
   each dimension in half - the earlier half getting the extra cell if
   that dimension is odd), all of the first quadrant's cells are
   visited before any of the second's, and so on, with each quadrant
   splitting the same way in turn. This works for any width/height,
   not just square or power-of-two ones - a real pass's grid usually
   isn't either (a 512x300 image's first pass is a 4x3 grid) - by
   simply letting a 1-wide or 1-tall region skip the quadrants that
   would otherwise be empty (a 1xN or Nx1 region just splits along its
   only splittable dimension).
   
   True recursion, rather than an explicit stack, is fine here: this
   always runs to completion in a single call - nothing needs to
   pause partway through it - and the recursion depth is bounded by
   roughly log2 of the larger grid dimension, at most about 10 levels
   even at this project's finest, largest grid. */
static void MapIndexToQuadrantOrder(long index, short left, short top, short width, short height, short *outColumn, short *outRow) {
	short	halfWidth, halfHeight, rightWidth, bottomHeight;
	long	topLeftCount, topRightCount, bottomLeftCount;
	
	if (width == 1 && height == 1) {
		*outColumn = left;
		*outRow    = top;
		return;
	}
	
	halfWidth    = (width  + 1) / 2;
	halfHeight   = (height + 1) / 2;
	rightWidth   = width  - halfWidth;
	bottomHeight = height - halfHeight;
	
	topLeftCount  = (long) halfWidth  * halfHeight;
	topRightCount = (long) rightWidth * halfHeight;
	
	if (index < topLeftCount) {
		MapIndexToQuadrantOrder(index, left, top, halfWidth, halfHeight, outColumn, outRow);
		return;
	}
	index -= topLeftCount;
	
	if (index < topRightCount) {
		MapIndexToQuadrantOrder(index, left + halfWidth, top, rightWidth, halfHeight, outColumn, outRow);
		return;
	}
	index -= topRightCount;
	
	bottomLeftCount = (long) halfWidth * bottomHeight;
	
	if (index < bottomLeftCount) {
		MapIndexToQuadrantOrder(index, left, top + halfHeight, halfWidth, bottomHeight, outColumn, outRow);
		return;
	}
	index -= bottomLeftCount;
	
	MapIndexToQuadrantOrder(index, left + halfWidth, top + halfHeight, rightWidth, bottomHeight, outColumn, outRow);
}

/* AllocateOffscreenMonoStore()
   Manually allocates a plain BitMap the size of imageStart and wraps
   it in a GrafPort, the classic pre-Color QuickDraw offscreen-bitmap
   technique - see the comment above offscreenPort/offscreenBits/
   offscreenGWorld for why colour uses something different.
   
   Also (re)allocates gMonoShadeLevels here, unconditionally, rather
   than lazily whenever animation first gets turned on: this store is
   always immediately followed by a full render (RenderFractalOffscreen(),
   called right after AllocateOffscreenStore() succeeds), which
   populates every entry via RecordMonoShadeLevels() - so by the time
   animation could possibly run (it never runs mid-render - see
   AnimationTask() in mwColourCycle.c), the buffer is guaranteed
   accurate. Allocating it lazily on first use, instead, left it full
   of NewPtr()'s uninitialised memory with no render left to come
   along and overwrite it - real testing showed exactly that: the
   animated image was solid noise from the moment animation first
   turned on, unchanging in content (only in which pattern represented
   each garbage byte) because nothing ever wrote real values into it
   afterward. */
static Boolean AllocateOffscreenMonoStore(void) {
    short	storeWidth  = imageStart.right  - imageStart.left;
    short	storeHeight = imageStart.bottom - imageStart.top;
    long	storeRowBytes = ((long) (storeWidth + 15) / 16) * 2;
    Ptr		storeBaseAddr = NewPtr(storeRowBytes * (long) storeHeight);
    short	finestSize;
    
    if (storeBaseAddr == NULL)
        return false;
    
    offscreenBits.baseAddr = storeBaseAddr;
    offscreenBits.rowBytes = storeRowBytes;
    offscreenBits.bounds   = imageStart;
    
    OpenPort(&offscreenPort);
    SetPort(&offscreenPort);
    SetPortBits(&offscreenBits);
    offscreenPort.portRect = imageStart;
    RectRgn(offscreenPort.visRgn, &imageStart);
    ClipRect(&imageStart);
    
    if (gMonoShadeLevels != NULL) {
        DisposePtr((Ptr) gMonoShadeLevels);
        gMonoShadeLevels = NULL;
    }
    
    finestSize = CurrentFinestBlockSize();
    gMonoShadeLevelColumns = BlocksAcross(storeWidth,  finestSize);
    gMonoShadeLevelRows    = BlocksAcross(storeHeight, finestSize);
    gMonoShadeLevels = (unsigned char *) NewPtr((long) gMonoShadeLevelColumns * gMonoShadeLevelRows);
    /* Not fatal if this one allocation fails - ShadeBlock()'s NULL
       check just means animation quietly has nothing to work from
       (see ApplyMonoPatternPhase()), not that rendering itself fails. */
    
    return true;
}

/* AllocateOffscreenColourStore()
   Creates an 8-bit indexed GWorld the size of imageStart, using our
   own fractal colour ramp rather than the system's default CLUT. The
   pixels are locked for the GWorld's whole lifetime (see
   DisposeOffscreenStore()) rather than around each individual draw,
   since it's small (well under 512K's headroom even though colour
   Macs never actually run this tight on memory) and locking once is
   one less thing to get wrong at every call site. */
static Boolean AllocateOffscreenColourStore(void) {
	CTabHandle	fractalColours = BuildFractalColourTable();
	QDErr		error;
	
	if (fractalColours == NULL)
		return false;
	
	error = NewGWorld(&offscreenGWorld, 8, &imageStart, fractalColours, NULL, 0);
	DisposeCTable(fractalColours);	/* NewGWorld() copies what it needs, per Inside Mac - see chat */
	
	if (error != noErr)
		return false;
	
	if (!LockPixels(((CGrafPtr) offscreenGWorld)->portPixMap)) {
		DisposeGWorld(offscreenGWorld);
		return false;
	}
	
	return true;
}

/* AllocateOffscreenStore()
   Picks monochrome or colour storage and, on success, records the
   bounds every caller shares regardless of which technology backed it. */
static Boolean AllocateOffscreenStore(void) {
	if (!(gHasColourQD ? AllocateOffscreenColourStore() : AllocateOffscreenMonoStore()))
		return false;
	
	offscreenBounds = imageStart;
	offscreenReady  = true;
	return true;
}

/* DisposeOffscreenStore()
   Frees whichever offscreen store is currently allocated so it can be
   reallocated at a new size (see HandleWindowResized()). Safe to call
   when nothing is currently allocated.
   
   Also frees gMonoShadeLevels, if allocated: it's sized for the
   current image, so it would be the wrong size for whatever gets
   allocated next. AllocateOffscreenMonoStore() reallocates it
   fresh, at whatever new size applies, the next time a mono store is
   allocated - so nothing needs to eagerly reallocate it here.
   
   Also frees the default-view cache (see gDefaultViewCachePixels et
   al.), for the same reason - it's sized for the current image too,
   and CacheOffscreenAsDefaultViewIfApplicable() rebuilds it fresh the
   next time the current fractal's default view finishes rendering. */
static void DisposeOffscreenStore(void) {
    if (offscreenReady) {
        if (gHasColourQD) {
            UnlockPixels(((CGrafPtr) offscreenGWorld)->portPixMap);
            DisposeGWorld(offscreenGWorld);
            offscreenGWorld = NULL;
        } else {
            ClosePort(&offscreenPort);
            DisposePtr(offscreenBits.baseAddr);
            offscreenBits.baseAddr = NULL;
        }
        
        offscreenReady = false;
    }
    
    if (gMonoShadeLevels != NULL) {
        DisposePtr((Ptr) gMonoShadeLevels);
        gMonoShadeLevels = NULL;
    }
    
    if (gDefaultViewCachePixels != NULL) {
        DisposePtr(gDefaultViewCachePixels);
        gDefaultViewCachePixels     = NULL;
        gDefaultViewCachePixelsSize = 0;
    }
    if (gDefaultViewCacheShadeLevels != NULL) {
        DisposePtr((Ptr) gDefaultViewCacheShadeLevels);
        gDefaultViewCacheShadeLevels = NULL;
    }
    gDefaultViewCacheWidth = 0;
}

/* BeginRendering()/EndRendering()
   The only two places fractalRenderJob.active changes, so the window
   title - which should read one way while a render is in progress and
   another once it's finished, been superseded, or never started - can
   never drift out of sync with it.
   
   Both of these can be called while the offscreen GWorld, not the
   window, is the current port (EndRendering() in particular fires
   from deep inside the block-drawing loop the instant the finest pass
   completes, and on every very first render regardless of what's
   selected, since the default width matches no fractal). SetWTitle()
   is explicitly given the window as a parameter, but its title-bar
   redraw appears not to be as indifferent to the current port as
   that suggests - so the port is saved, forced to the window, and
   restored around it, rather than trusting whatever was current. */
static void BeginRendering(void) {
	GrafPtr savedPort;
	
	fractalRenderJob.active = true;
	haveAccumulatedChanges  = false;
	
	GetPort(&savedPort);
	SetPort(mwWindow);
	SetWTitle(mwWindow, kRenderingWindowTitle);
	SetPort(savedPort);
}

static void EndRendering(void) {
	GrafPtr savedPort;
	
	fractalRenderJob.active  = false;
	fractalRenderJob.endTick = TickCount();
	
	/* Whether or not this render ever actually allocated the symmetry
	   cache (SymmetryFoldEligible() might have been false the whole
	   time), FreeSymmetryCache() is always safe to call - it's a no-op
	   when gSymmetryCache is already NULL. Both ways a render can end -
	   BeginNextPass()'s natural completion and AbortFractalRender()'s
	   early stop - call this one function, so this is the single place
	   that needs to free it, the same reasoning DisposeOffscreenStore()
	   already applies to the offscreen buffer itself. */
	FreeSymmetryCache();
	
	GetPort(&savedPort);
	SetPort(mwWindow);
	SetWTitle(mwWindow, kIdleWindowTitle);
	SetPort(savedPort);
}

/* IsRenderActive()
   Whether a render is currently under way - used both by the Get Info
   window (to choose "rendering..." vs "render time") and by
   mwMenus.c's AdjustMenus() to grey out Save As while a render is in
   progress, since the offscreen store is still being written to. */
Boolean IsRenderActive(void) {
	return fractalRenderJob.active;
}

/* RenderElapsedTicks()
   Ticks (60ths of a second) the current or most recent render has
   taken: live, counted from startTick, while active; fixed, from the
   startTick/endTick pair EndRendering() recorded, once it's finished -
   whether that's by completing normally or being aborted. Aborting
   deliberately leaves the image and its timing exactly as they were
   at that point, rather than clearing either. */
unsigned long RenderElapsedTicks(void) {
	if (fractalRenderJob.active)
		return TickCount() - fractalRenderJob.startTick;
	
	return fractalRenderJob.endTick - fractalRenderJob.startTick;
}

/* CurrentFractalName()
   A display name for whatever "width" currently selects, read from
   the same kFractalTypes[] table FractalTypeNameForWidth() reads -
   this used to be its own separate width==1/2/3 chain, one more place
   a new type's name needed adding by hand. Builds into a static
   buffer (a Pascal string constant like the old "\pMandelbrot"
   literals can't hold FractalTypeNameForWidth()'s runtime C string)
   using the same plain byte-copy AppendCString() (mwInfo.c) uses for
   the same job - safe here since kFractalTypes[] names are all
   well under Str255's 255-byte limit. */
ConstStr255Param CurrentFractalName(void) {
	static Str255	name;
	const char		*cName = FractalTypeNameForWidth(width);
	short			i = 0;
	
	if (cName[0] == '\0')
		return "\p(none selected)";
	
	while (cName[i] != '\0' && i < 255) {
		name[i + 1] = cName[i];
		i++;
	}
	name[0] = i;
	
	return name;
}

/* GetFractalResolution()
   The image's current width and height in pixels. A function rather
   than exposing windowWidth/windowHeight directly, since those are
   fixed #defines today but won't necessarily stay fixed once the
   resizing work (#5) lands. */
void GetFractalResolution(short *outWidth, short *outHeight) {
	*outWidth  = windowWidth;
	*outHeight = windowHeight;
}

/* EstimateOffscreenBytesNeeded()
   See mwWindow.h. A deliberate overestimate - rounds generously and
   adds a fixed margin for the GWorld's/BitMap's own small internal
   structures - rather than an exact figure matching
   AllocateOffscreenColourStore()/AllocateOffscreenMonoStore()'s own
   real sizing. Good enough to decide whether an allocation is likely
   to fail before ever attempting it (see mwZoom.c's TrackWindowResize()),
   not a substitute for those functions' own exact calculations. */
long EstimateOffscreenBytesNeeded(short width, short height) {
	if (gHasColourQD) {
		return (long) width * height + 2048L;
	} else {
		short	finestSize      = CurrentFinestBlockSize();
		long	bitmapBytes     = ((long) (width + 15) / 16) * 2 * height;
		long	shadeLevelBytes = ((long) width / finestSize) * ((long) height / finestSize);
		
		return bitmapBytes + shadeLevelBytes + 1024L;
	}
}

/* GetFractalParameters()
   See the FractalParameters comment in mwWindow.h for which fields
   apply to which fractal.
   
   zoom is derived from gView.halfWidthRe as pixels-per-unit
   (windowWidth / (2*halfWidthRe)) - the same quantity the old fixed
   kMandelbrotZoom/kJuliaZoom constants represented, so this reads
   exactly 150 at Mandelbrot's default view and 170.667 at Julia's
   (matching windowWidth/(2*1.5)), and grows larger as gView zooms in
   via the marquee (mwZoom.c), staying consistent with what this
   field always meant rather than switching to some new unit. */
FractalParameters GetFractalParameters(void) {
	FractalParameters params;
	const FractalTypeDescriptor *descriptor = DescriptorForWidth(width);
	
	params.zoom          = 0.0;
	params.maxIterations = 0;
	params.constantRe    = 0.0;
	params.constantIm    = 0.0;
	
	if (descriptor == NULL)
		return params;
	
	if (FractalTypeHasView(width))
		params.zoom = windowWidth / (2.0 * gView.halfWidthRe);
	
	/* maxIterations is meaningful for escape-time AND convergence types
	   (Newton's own kNewtonMaxIterations really is a per-type ceiling
	   worth reporting, same as escape-time's) but not Lyapunov
	   (kFractalFamilyStatistical): its registry row's maxIterations is
	   a placeholder, since its real iteration counts are fixed inside
	   mwLyapunovMath.c, not something this per-type field describes.
	   The constant is escape-time-specific only - Julia's, so far. */
	if (descriptor->family == kFractalFamilyEscapeTime || descriptor->family == kFractalFamilyConvergence)
		params.maxIterations = descriptor->maxIterations;
	
	if (descriptor->hasFixedConstant) {
		params.constantRe = descriptor->constantRe;
		params.constantIm = descriptor->constantIm;
	}
	
	return params;
}

/* UpdateIterationCeilingForBlockSize()
   Coarse blocks get overdrawn by finer ones moments later, so they
   don't need the full iteration ceiling to be useful as a preview - a
   cheaper, reduced one that's still enough to show roughly the right
   shade is plenty, right up until the finest block size, which
   determines the final image and must use the real ceiling. The
   reduction scales with how coarse blockSize is relative to the
   finest one, floored at kMinimumIterationCeiling so even the
   coarsest block still shows some differentiation rather than none.
   Takes blockSize as a parameter, called with fractalRenderJob.blockSize
   at each pass transition, rather than reading that field internally -
   a small, harmless indirection that happened to make an earlier,
   since-abandoned per-region traversal easier to try without
   reshaping this function too.
   
   This only reduces the *preview*, not the final result: at
   blockSize == finestSize, the reduction factor is 1, and
   currentIterationCeiling is exactly the fractal's real ceiling -
   unchanged from before this existed. */
static void UpdateIterationCeilingForBlockSize(short blockSize) {
	const FractalTypeDescriptor *descriptor = DescriptorForWidth(width);
	short fullCeiling = (descriptor != NULL) ? descriptor->maxIterations : kMandelbrotMaxIterations;
	short finestSize  = CurrentFinestBlockSize();
	short reduction   = blockSize / finestSize;
	short reduced     = fullCeiling / reduction;
	
	if (reduced < kMinimumIterationCeiling)
		reduced = kMinimumIterationCeiling;
	if (reduced > fullCeiling)
		reduced = fullCeiling;
	
	currentIterationCeiling = reduced;
}

/* StartProgressiveRender()
   Resets the render job to its coarsest pass. Nothing is drawn here -
   AdvanceFractalRender() draws the first block the next time it's
   called - so this returns immediately regardless of how big or slow
   the fractal turns out to be. */
static void StartProgressiveRender(FractalSampleProc sampleProc) {
	short imageWidth  = offscreenBounds.right  - offscreenBounds.left;
	short imageHeight = offscreenBounds.bottom - offscreenBounds.top;
	short longerSide  = (imageHeight > imageWidth) ? imageHeight : imageWidth;
	short finestSize  = CurrentFinestBlockSize();
	
	fractalRenderJob.sampleProc = sampleProc;
	fractalRenderJob.blockSize  = HighestPowerOfTwoAtMost(longerSide / kBlockGridTargetColumns);
	if (fractalRenderJob.blockSize < finestSize)
		fractalRenderJob.blockSize = finestSize;
	
	fractalRenderJob.columnCount     = BlocksAcross(imageWidth,  fractalRenderJob.blockSize);
	fractalRenderJob.rowCount        = BlocksAcross(imageHeight, fractalRenderJob.blockSize);
	fractalRenderJob.nextBlockIndex  = 0;
	UpdateIterationCeilingForBlockSize(fractalRenderJob.blockSize);
	PrepareRenderMapping();
	
	/* Only ever needed by the finest, one-pixel pass (see
	   SampleWithSymmetryFold()'s own comment) - allocated once, here,
	   for the render as a whole rather than re-checked pass by pass,
	   since neither the type nor the view can change mid-render. Freed
	   in EndRendering(), whichever of the two ways this render ends. */
	if (SymmetryFoldEligible())
		AllocateSymmetryCache();
	
	BeginRendering();
}

/* DrawNextBlockAndAdvance()
   Samples and shades the single next block in the current pass, then
   moves on to the following block (or the next, finer pass, or
   completion). "Next" is in quadrant order - see
   MapIndexToQuadrantOrder() - not row-major. Assumes the offscreen
   port is already current. Reports the block's (clipped) rect via
   drawnRect, so AdvanceFractalRender() can union it with whatever
   else it draws in the same call and blit only that combined region
   afterward, rather than the whole image.
   
   A Mariani-Silver-style "confirm this block uniform from its border,
   then skip it on every later pass" scheme was tried twice here, at
   two different levels of caution - first checking 4 corners at any
   block size, then 9 border points restricted to blocks 8px or
   smaller - and abandoned both times after real testing showed a
   visibly wrong render. The underlying problem turned out to be more
   fundamental than either attempt's own specifics: the Mandelbrot/
   Julia boundary is a genuine fractal, with real structure at every
   scale, so there is no block size small enough to make "check a
   handful of border points" a safe stand-in for "every pixel inside
   is the same" - shrinking the block doesn't make the boundary less
   detailed relative to it. Worse, the overhead lands exactly where it
   can't pay off: confirmation succeeds least often near the boundary,
   which is also where nearly all of a render's actual time goes, so
   the extra samples spent attempting confirmation there are closer to
   pure loss than a trade-off. Both attempts made real, measured
   renders slower, not faster, consistent with this. Not something to
   keep narrowing by guesswork - if this is revisited, it needs a
   fundamentally different approach, not a smaller threshold. */
static void DrawNextBlockAndAdvance(Rect *drawnRect) {
	Rect	blockRect, clippedRect;
	short	sampleX, sampleY;
	short	column, row;
	short	left, top;
	
	MapIndexToQuadrantOrder(fractalRenderJob.nextBlockIndex, 0, 0,
			fractalRenderJob.columnCount, fractalRenderJob.rowCount, &column, &row);
	
	left = column * fractalRenderJob.blockSize;
	top  = row    * fractalRenderJob.blockSize;
	
	SetRect(&blockRect, left, top, left + fractalRenderJob.blockSize, top + fractalRenderJob.blockSize);
	SectRect(&blockRect, &offscreenBounds, &clippedRect);
	
	sampleX = clippedRect.left + (clippedRect.right  - clippedRect.left) / 2;
	sampleY = clippedRect.top  + (clippedRect.bottom - clippedRect.top)  / 2;
	
	/* SampleWithSymmetryFold() only ever applies at the finest,
	   one-pixel-per-block pass - see its own comment for why coarser
	   passes aren't worth the added bookkeeping. blockSize==1 is
	   colour-only in practice (mono's own finest is 2x2 -
	   CurrentFinestBlockSize()), so mono renders always take the plain
	   path here, unaffected. */
	if (fractalRenderJob.blockSize == 1)
		ShadeBlock(&clippedRect, SampleWithSymmetryFold(sampleX, sampleY));
	else
		ShadeBlock(&clippedRect, fractalRenderJob.sampleProc(sampleX, sampleY));
	
	*drawnRect = clippedRect;
	
	AdvanceToNextBlock();
}

/* AdvanceToNextBlock()
   Moves the job on to the next index in quadrant order, or on to the
   next (finer) pass once every block in the current pass has been
   drawn. */
static void AdvanceToNextBlock(void) {
	fractalRenderJob.nextBlockIndex++;
	
	if (fractalRenderJob.nextBlockIndex < (long) fractalRenderJob.columnCount * fractalRenderJob.rowCount)
		return;
	
	BeginNextPass();
}

/* BeginNextPass()
   Halves the block size and rebuilds the whole-image grid for the
   next, finer pass - or marks the job finished once blocks are
   already as fine as CurrentFinestBlockSize() allows. Every block at
   the current size is shaded before any of them subdivides further,
   so the whole image refines together, pass by pass, rather than one
   region reaching full detail while the rest wait - two earlier
   attempts at a per-region "resolve this one fully, then the next"
   traversal both turned out not to be what was actually wanted here. */
static void BeginNextPass(void) {
	if (fractalRenderJob.blockSize <= CurrentFinestBlockSize()) {
		CacheOffscreenAsDefaultViewIfApplicable();
		EndRendering();
		return;
	}
	
	fractalRenderJob.blockSize     /= 2;
	fractalRenderJob.columnCount     = BlocksAcross(offscreenBounds.right  - offscreenBounds.left, fractalRenderJob.blockSize);
	fractalRenderJob.rowCount        = BlocksAcross(offscreenBounds.bottom - offscreenBounds.top,  fractalRenderJob.blockSize);
	fractalRenderJob.nextBlockIndex  = 0;
	UpdateIterationCeilingForBlockSize(fractalRenderJob.blockSize);
}

/* EnterOffscreenPort()/EnterWindowPort()
   Makes the offscreen store, or the window, the current port, via
   plain SetPort() in both colour and monochrome. Color QuickDraw
   drawing normally depends on the current GDevice as well as the
   current port, which is what SetGWorld()/GetGWorld() are for - but
   on real testing, SetGWorld(offscreenGWorld, NULL) itself reliably
   crashed even though NewGWorld()/LockPixels() on that same GWorld
   worked fine moments earlier, so GetGWorld()/SetGWorld() are avoided
   here entirely rather than chased further. Nothing here should
   actually need GDevice-tracking: RGBForeColor()/PaintRect() only
   ever target the offscreen GWorld, which carries its own colour
   table, and the window only ever receives a plain CopyBits(). If
   colours come out wrong once this is drawing again, that assumption
   is the first thing to revisit. */
static void EnterOffscreenPort(void) {
	SetPort(gHasColourQD ? (GrafPtr) offscreenGWorld : &offscreenPort);
}

static void EnterWindowPort(void) {
	SetPort(mwWindow);
}

/* GetOffscreenImage()
   Hands back the offscreen store's pixel data and bounds for whatever
   is currently rendered - complete, partial, or aborted, it doesn't
   matter here; this just describes whatever is actually in the
   buffer right now. Used by BlitOffscreenToWindow() below and by
   Save As (mwSaveAs.c). Returns false, leaving *bits/*bounds
   untouched, if there's no offscreen store at all (allocation failed
   under low memory).
   
   &mwWindow->portBits-style coercion isn't needed here the way it is
   for the window: offscreenGWorld's own portBits works the same way
   for the identical documented reason (a CGrafPtr coerced to GrafPtr
   and read as .portBits is how Inside Macintosh says to get a
   CopyBits-compatible pointer from a colour port), it's just a
   different backing store than the window, hence the branch. */
Boolean GetOffscreenImage(BitMap **bits, Rect *bounds) {
	if (!offscreenReady)
		return false;
	
	*bits   = gHasColourQD ? &((GrafPtr) offscreenGWorld)->portBits : &offscreenBits;
	*bounds = offscreenBounds;
	return true;
}

/* GetOffscreenColourTable()
   Returns the offscreen colour GWorld's own CTabHandle - the actual
   one it owns internally, not the temporary one BuildFractalColourTable()
   built and NewGWorld() copied from at allocation time (that copy is
   disposed immediately after, in AllocateOffscreenColourStore()). A
   PixMap's pmTable field is exactly this: the colour table QuickDraw
   itself consults whenever it needs to interpret that PixMap's index
   bytes as colours - which CopyBits() does on every blit. Rotating
   this table's own RGB entries in place, then blitting the (entirely
   unchanged) pixel data again via RefreshWholeDisplay(), is how
   mwColourCycle.c's Animate feature shows different colours without
   redrawing a single pixel - real Mac colour-cycling, not a
   recompute-and-redraw pretending to be one. */
CTabHandle GetOffscreenColourTable(void) {
	if (!gHasColourQD || !offscreenReady)
		return NULL;
	
	return (**((CGrafPtr) offscreenGWorld)->portPixMap).pmTable;
}

Boolean IsRenderingInColour(void) {
	return ShouldRenderInColour();
}

/* IsAnimationAvailable()
   See mwWindow.h. Colour animation works the same way for every
   fractal type, direct-draw or sampled, since they all draw through
   the same shared indexed-colour/palette machinery - but mono has
   nothing to animate for a direct-draw type (the Tree; eventually
   Fern/Sierpinski): those draw lines/points straight via MoveTo()/
   Line(), with no stored per-pixel shade level the way ShadeBlock()'s
   own path keeps in gMonoShadeLevels for every sampled fractal, so
   there's nothing for a "phase" to re-dither against. */
Boolean IsAnimationAvailable(void) {
	if (!HasRenderableImage())
		return false;
	
	if (ShouldRenderInColour())
		return true;
	
	return (gMonoShadeLevels != NULL) && (FractalFamilyForWidth(width) != kFractalFamilyRecursive);
}

/* RefreshWholeDisplay()
   Blits the whole current offscreen image to the window, exactly like
   an update event's DrawContent() call would, but callable any time -
   for mwColourCycle.c to show a rotated colour table or a phase-shifted
   pattern redraw without waiting for or forcing an actual update
   event. */
void RefreshWholeDisplay(void) {
	EnterWindowPort();
	BlitOffscreenToWindow(NULL);
}

/* ApplyMonoPatternPhase()
   Redraws every finest-grid cell of the mono offscreen image from its
   already-recorded shade level (see RecordMonoShadeLevels(), and
   AllocateOffscreenMonoStore() for why gMonoShadeLevels is always
   accurate by the time this can run at all) and the given phase, via
   the same MonoBandIndexForShadeLevel()/FillMonoBand() ladder normal
   rendering uses - only pattern lookups and FillRect calls, no
   fractal math. Leaves the result in the offscreen store; the caller
   still needs RefreshWholeDisplay() to show it.
   
   Coalesces each row into runs of consecutive cells sharing the same
   band and fills a run with one FillRect call, rather than one call
   per cell: real testing showed the naive one-call-per-cell version
   running at roughly 1 frame per second even on a 68040, since
   FillRect's fixed per-call cost dominates at cell size (as small as
   2x2 pixels) and a typical fractal image has large, uniform
   stretches - the escaped background especially - where this
   coalescing collapses hundreds of calls into one. Detailed,
   fast-varying regions still take one call per cell or close to it,
   so this doesn't fully close the gap with real colour-cycling
   (which touches no pixels at all) - there's no way around that with
   a technology that has no indirection to exploit, only less of a
   redraw to do.
   
   Declines to do anything if gMonoShadeLevels doesn't exist (the
   offscreen store failed to allocate under low memory - see
   AllocateOffscreenMonoStore()) or if a render is currently in
   progress: mid-render, gMonoShadeLevels is a mix of accurate
   finished-pass values and coarser not-yet-refined ones (see
   RecordMonoShadeLevels()), which would show as visible blockiness
   rather than the actual image. */
void ApplyMonoPatternPhase(short phase) {
	short	finestSize = CurrentFinestBlockSize();
	short	column, row;
	GrafPtr	savedPort;
	
	if (gMonoShadeLevels == NULL || IsRenderActive())
		return;
	
	GetPort(&savedPort);
	EnterOffscreenPort();
	
	for (row = 0; row < gMonoShadeLevelRows; row++) {
		unsigned char *rowLevels     = gMonoShadeLevels + (long) row * gMonoShadeLevelColumns;
		short          runStartColumn = 0;
		short          runBandIndex   = MonoBandIndexForShadeLevel(rowLevels[0], phase);
		short          top            = row * finestSize;
		
		for (column = 1; column <= gMonoShadeLevelColumns; column++) {
			short bandIndex = (column < gMonoShadeLevelColumns)
					? MonoBandIndexForShadeLevel(rowLevels[column], phase)
					: -1;	/* forces the final run in the row to flush below */
			
			if (bandIndex != runBandIndex) {
				Rect runRect;
				
				SetRect(&runRect, runStartColumn * finestSize, top, column * finestSize, top + finestSize);
				FillMonoBand(&runRect, runBandIndex);
				
				runStartColumn = column;
				runBandIndex   = bandIndex;
			}
		}
	}
	
	SetPort(savedPort);
}

/* EnsureWindowVisible()
   mwWindow is created once at launch (see SetUpWindow(), called from
   main()) and never disposed - its close box only hides it (see
   HandleEvent()'s inGoAway case) - so "no window is open" always means
   "mwWindow exists but isn't visible", never a NULL window to create.
   Calling SetUpWindow() again here would create a second, duplicate
   window rather than reopening this one. */
void EnsureWindowVisible(void) {
	if (((WindowPeek) mwWindow)->visible)
		return;
	
	ShowWindow(mwWindow);
	SelectWindow(mwWindow);
}

/* HasRenderableImage()
   Whether there's anything meaningful to read from the offscreen
   store right now - true once a render has ever been allocated
   (offscreenReady), regardless of whether it's still in progress,
   finished, or was aborted partway through; false if none ever has -
   on a fresh launch (width still kNoFractalSelectedWidth, nothing
   picked yet) or if the one attempt failed under low memory (see
   AllocateOffscreenStore()). Used to gate Save As (both PICT and
   fractal-data), zooming in and out, and Animate - none of them mean
   anything against a window that's never actually rendered anything. */
Boolean HasRenderableImage(void) {
	return offscreenReady;
}

/* IsZoomAvailable()
   True while zooming - in (mwZoom.c's marquee tracking and keyboard
   zoom) or out (the Zoom Out menu item, and keyboard zoom's other
   direction) - means anything right now: the current fractal (width)
   must actually have a zoomable view (FractalTypeHasView() - true for
   any family except the Tree's, which doesn't use gView at all), and
   something must have been rendered at all (HasRenderableImage()) for
   a zoom to have anything meaningful to act on. */
Boolean IsZoomAvailable(void) {
	return FractalTypeHasView(width) && HasRenderableImage();
}

/* StartNewFractal()
   "New Fractal" (mwMenus.c): resets to the same "nothing selected
   yet" state the app launches into - disposes whatever's currently
   rendered (DisposeOffscreenStore(), safe to call regardless of
   whether anything was actually allocated) and sets width back to
   kNoFractalSelectedWidth, so the window goes blank until the person
   picks a fractal type from the Fractal menu again, exactly as it
   does on a fresh launch. Doesn't touch gView or the current palette -
   ResetViewForCurrentFractal() already runs whenever a fractal type
   is next chosen, and the palette is a display preference independent
   of any one fractal's own state (see currentPalette's own comment). */
void StartNewFractal(void) {
	DisposeOffscreenStore();
	width = kNoFractalSelectedWidth;
}

/* FractalTypeNameForWidth()/FindFractalTypeByWidth()
   Map width (the fractal-type selector) to and from its name as a
   plain C string, for saving/loading fractal parameters (see
   mwSaveAs.c's SaveFractalData()/LoadFractalData()) - a name rather
   than width's raw numeric value, so a saved file's meaning survives
   even if new fractal types are ever inserted ahead of existing ones
   (see next-improvements.md's §5 for what those might be).
   FractalTypeNameForWidth() returns "" for kNoFractalSelectedWidth or
   anything else unrecognised - callers only ever call it once a
   fractal has actually been rendered (HasRenderableImage()), so this
   case shouldn't be reached in practice, but returning an empty name
   rather than a garbage one does no harm if it somehow is.
   FindFractalTypeByName() returns false (leaving *outWidth untouched)
   for a name it doesn't recognise, so a saved file naming a fractal
   type this build doesn't have yet fails that one line rather than
   the whole load. */
const char *FractalTypeNameForWidth(short widthValue) {
	const FractalTypeDescriptor *descriptor = DescriptorForWidth(widthValue);
	
	return (descriptor != NULL) ? descriptor->name : "";
}

Boolean FindFractalTypeByName(const char *name, short *outWidth) {
	short i;
	
	for (i = 0; i < (short) kFractalTypeCount; i++) {
		if (strcmp(name, kFractalTypes[i].name) == 0) {
			*outWidth = kFractalTypes[i].typeID;
			return true;
		}
	}
	
	return false;
}

/* FractalFamilyForWidth()/FractalTypeHasFixedConstant()
   See their own comments in mwWindow.h. Falls back to
   kFractalFamilyRecursive/false for kNoFractalSelectedWidth or
   anything else unrecognised - the same "no descriptor, do the
   inert thing" fallback DescriptorForWidth()'s own callers already
   use throughout this file. */
FractalFamily FractalFamilyForWidth(short widthValue) {
	const FractalTypeDescriptor *descriptor = DescriptorForWidth(widthValue);
	
	return (descriptor != NULL) ? descriptor->family : kFractalFamilyRecursive;
}

Boolean FractalTypeHasView(short widthValue) {
	return FractalFamilyForWidth(widthValue) != kFractalFamilyRecursive;
}

Boolean FractalTypeHasFixedConstant(short widthValue) {
	const FractalTypeDescriptor *descriptor = DescriptorForWidth(widthValue);
	
	return (descriptor != NULL) ? descriptor->hasFixedConstant : false;
}

/* RebuildOffscreenColourTableForCurrentPalette()
   Overwrites every entry of the offscreen GWorld's own colour table
   (see GetOffscreenColourTable()) with ColourForShadeLevel()'s output
   under whichever palette is now current - the same computation
   BuildFractalColourTable() uses to build a table from scratch,
   applied here to one that already exists, in place, rather than
   allocating a new one. The pixel data (shade-level indices) is
   completely untouched, so a palette change never needs a re-render -
   only a fresh blit (see SetCurrentPalette()) to show the same,
   already-computed indices through their new colours. Mirrors
   mwColourCycle.c's RotateColourTable() in spirit - direct mutation of
   the live colour table, then GetCTSeed() to mark it changed, no
   SetGWorld()/Palette Manager involved, for the same reasons spelled
   out there. */
static void RebuildOffscreenColourTableForCurrentPalette(void) {
	CTabHandle	table = GetOffscreenColourTable();
	short		i;
	
	if (table == NULL)
		return;
	
	/* Only 0..kShadingScale - never kBackgroundWhiteIndex/kBackgroundBlackIndex,
	   which stay a fixed white and black regardless of palette (see
	   their own comment). table's own ctSize+1 now covers those two
	   as well, so it isn't used as the loop bound here any more. */
	for (i = 0; i <= kShadingScale; i++)
		(**table).ctTable[i].rgb = ColourForShadeLevel(i);
	
	(**table).ctSeed = GetCTSeed();
}

/* GetRotatableColourTableEntryCount()
   See mwWindow.h - how many of the offscreen colour table's entries
   Animate's own colour-table rotation (mwColourCycle.c's
   RotateColourTable()) may touch: kShadingScale+1, the palette-driven
   shading range, and never the two fixed background entries beyond
   it (see kBackgroundWhiteIndex/kBackgroundBlackIndex's own comment) -
   rotating those into the shading range would eventually leave a
   flat white or black smeared across part of the ramp. */
short GetRotatableColourTableEntryCount(void) {
	return kShadingScale + 1;
}

/* GetPaletteCount()
   How many palettes exist - GetCurrentPalette()/SetCurrentPalette()
   work in terms of a 0-based index below this. */
short GetPaletteCount(void) {
	return kPaletteCount;
}

/* GetCurrentPalette()
   The currently active palette's 0-based index into kPalettes[]. */
short GetCurrentPalette(void) {
	return currentPalette;
}

/* GetPaletteName()/FindPaletteByName()
   Map a palette's index to and from its name (kPalettes[]'s own name
   field - see its comment), for saving/loading fractal parameters
   (see mwSaveAs.c's SaveFractalData()/LoadFractalData()) - a name
   rather than a raw index, so a saved file's meaning survives even if
   palettes are ever reordered or new ones inserted ahead of existing
   ones. GetPaletteName() returns "" for an out-of-range index, which
   shouldn't be reached in practice since callers only ever pass
   GetCurrentPalette()'s own result. FindPaletteByName() returns false
   (leaving *outIndex untouched) for a name it doesn't recognise, so a
   saved file naming a palette this build doesn't have fails that one
   line rather than the whole load - LoadFractalData() leaves the
   current palette unchanged in that case. */
const char *GetPaletteName(short paletteIndex) {
	if (paletteIndex < 0 || paletteIndex >= kPaletteCount)
		return "";
	return kPalettes[paletteIndex].name;
}

Boolean FindPaletteByName(const char *name, short *outIndex) {
	short i;
	for (i = 0; i < kPaletteCount; i++) {
		if (strcmp(kPalettes[i].name, name) == 0) {
			*outIndex = i;
			return true;
		}
	}
	return false;
}

/* IsPaletteAvailable()
   True while palette selection means anything at all. A palette is a
   colour-table concept, meaningless for the monochrome dither-pattern
   path, so this just mirrors gHasColourQD - mwMenus.c uses this to
   grey out the Palette submenu on a black-and-white Mac. */
Boolean IsPaletteAvailable(void) {
	return gHasColourQD;
}

/* SetCurrentPalette()
   See mwWindow.h. Deliberately doesn't check IsRenderActive(): unlike
   RestoreDefaultViewFromCache(), this never touches the offscreen
   store's pixel data, only the colour table's RGB entries, so there's
   no actual race with an in-progress render to guard against - safe
   to call at any time, including from a future fractal-data loader
   (see next-improvements.md) that might want to set the palette
   before triggering a fresh render. mwMenus.c still greys out the
   menu item during a render anyway, for consistency with how
   Animate/Zoom Out/Save As behave, even though this specific
   operation wouldn't actually be unsafe mid-render. */
void SetCurrentPalette(short paletteIndex) {
	if (paletteIndex < 0 || paletteIndex >= kPaletteCount)
		return;
	
	currentPalette = paletteIndex;
	
	RebuildOffscreenColourTableForCurrentPalette();
	RefreshWholeDisplay();
}

/* RestoreDefaultViewFromCache()
   See mwWindow.h. Restoring the cache doesn't touch fractalRenderJob
   or the window title: this can only be reached while IsRenderActive()
   is already false (mwMenus.c greys out Zoom Out otherwise, matching
   Animate/Save As), so both are already sitting at "idle" from
   whatever render last completed, and there's nothing here that needs
   to change either. */
Boolean RestoreDefaultViewFromCache(void) {
	BitMap	*bits;
	Rect	bounds;
	
	if (IsRenderActive())
		return false;
	
	if (gDefaultViewCacheWidth != width || gDefaultViewCachePixels == NULL)
		return false;
	
	if (!GetOffscreenImage(&bits, &bounds))
		return false;
	
	if ((long) bits->rowBytes * (bounds.bottom - bounds.top) != gDefaultViewCachePixelsSize)
		return false;
	
	BlockMove(gDefaultViewCachePixels, bits->baseAddr, gDefaultViewCachePixelsSize);
	
	if (!gHasColourQD && gMonoShadeLevels != NULL && gDefaultViewCacheShadeLevels != NULL) {
		long shadeLevelsSize = (long) gMonoShadeLevelColumns * gMonoShadeLevelRows;
		BlockMove(gDefaultViewCacheShadeLevels, gMonoShadeLevels, shadeLevelsSize);
	}
	
	return true;
}

/* BlitOffscreenToWindow()
   Copies the offscreen store onto the window - either the whole
   thing (changedRect NULL, for DrawContent(), which doesn't know what
   specifically needs repainting) or just changedRect (for
   AdvanceFractalRender(), which does: the region its last handful of
   blocks actually touched). Copying only what changed avoids
   re-copying the entire image on every idle tick regardless of how
   little of it is new - by rough count, on the order of 7-8 GB of
   redundant copying over one full render at the image's full size,
   almost all of it pixels that hadn't changed since the previous
   tick.
   
   Assumes the caller has already called EnterWindowPort() (both
   DrawContent() and AdvanceFractalRender() do this themselves, since
   each needs the port/device set for its own reasons too). */
static void BlitOffscreenToWindow(const Rect *changedRect) {
    BitMap	*sourceBits;
    Rect	sourceBounds;
    Rect	blitRect;
    
    if (!GetOffscreenImage(&sourceBits, &sourceBounds))
        return;
    
    if (!((WindowPeek) mwWindow)->visible)
        return;
    
    if (changedRect != NULL)
        SectRect(changedRect, &sourceBounds, &blitRect);
    else
        blitRect = sourceBounds;
    
    /* The offscreen store and the window's content area share the same
       coordinate space - both originate at (0,0) at the same size - so
       blitRect serves as both the source and destination rect here. */
    CopyBits(sourceBits, &mwWindow->portBits, &blitRect, &blitRect, srcCopy, NULL);
}

/* RenderFractalOffscreen()
   Starts (or restarts) rendering into the offscreen store: the tree is
   cheap enough to just draw outright, but Mandelbrot and Julia only
   have their first, coarsest pass queued up here - AdvanceFractalRender()
   does the actual work, a little at a time, from HandleEvent()'s idle
   time, so the image comes into focus instead of the app looking
   hung. Call this only when the fractal's parameters actually change:
   the fractal type (HandleMenu()'s fractalID case), a window resize
   (HandleWindowResized()), or a zoom (mwZoom.c) - never from an
   ordinary update event.
   
   If the offscreen store isn't available and can't be allocated (low
   memory), this does nothing; DrawContent() then falls back to
   recomputing the fractal directly into the window on every update. */
void RenderFractalOffscreen(void) {
    GrafPtr	savedPort;
    
    GetPort(&savedPort);
    fractalRenderJob.startTick = TickCount();
    
    if (!offscreenReady && !AllocateOffscreenStore()) {
        EndRendering();
        SetPort(savedPort);
        return;
    }
    
    EnterOffscreenPort();
    
	/* Every direct-draw type (the Tree; eventually Fern/Sierpinski) gets
	   a background chosen for contrast against whatever palette is
	   active (RecursiveFractalBackgroundIndex()) rather than the plain
	   white every sample-based fractal erases to - mono has no palette
	   to contrast against, so it keeps the ordinary erase unchanged. */
	if (FractalFamilyForWidth(width) == kFractalFamilyRecursive && gHasColourQD)
		FillIndexedRect(&offscreenBounds, RecursiveFractalBackgroundIndex());
	else
		EraseRect(&offscreenBounds);
    
	{
		const FractalTypeDescriptor *descriptor = DescriptorForWidth(width);
		
		if (descriptor == NULL) {
			EndRendering();
		} else if (descriptor->directDrawProc != NULL) {
			descriptor->directDrawProc();
			EndRendering();
			
			/* Direct-draw types (Tree, Fern, Sierpinski) finish their
			   whole render synchronously, right here - unlike the
			   sample-based path below, there's no ongoing progressive
			   job left for AdvanceFractalRender()'s own idle-time
			   blitting to pick up afterward, so without this, nothing
			   would ever tell the window to actually show what was
			   just drawn into the offscreen buffer. A caller that
			   already does its own InvalRect() afterward (HandleMenu()'s
			   fractal-selection code, mwMenus.c) never showed this gap;
			   HandleWindowResized() doesn't, and real testing (resizing
			   the window while viewing Fern) showed exactly what that
			   gap looks like - the window keeping its pre-resize
			   content, with only whatever area SizeWindow() itself
			   considered "newly exposed" getting an automatic update
			   event, a confusing "new drawing appearing on top of the
			   old" result rather than a blank or simply missing one.
			   Blitting explicitly here, rather than requiring every
			   caller to remember an InvalRect() of their own, fixes it
			   for any caller, present or future, not just this one. */
			{
				GrafPtr blitSavedPort;
				
				GetPort(&blitSavedPort);
				EnterWindowPort();
				BlitOffscreenToWindow(NULL);
				SetPort(blitSavedPort);
			}
		} else if (descriptor->sampleProc != NULL) {
			StartProgressiveRender(descriptor->sampleProc);
		} else {
			EndRendering();
		}
	}
    
    SetPort(savedPort);
}

/* AdvanceFractalRender()
   Draws up to kBlocksPerIdleSlice more blocks of whatever render job
   is in progress, then shows the progress so far on screen - but not
   necessarily every single time this is called; see
   kBlitIntervalTicks' own comment for why. Meant to be called from
   the main event loop's idle time (see MandyWindow.c's HandleEvent());
   does nothing if there's no job running. */
void AdvanceFractalRender(void) {
    GrafPtr	savedPort;
    short	blocksRemaining = kBlocksPerIdleSlice;
    Rect	changedRect, blockRect;
    
    if (!fractalRenderJob.active || !offscreenReady)
        return;
    
    GetPort(&savedPort);
    
    EnterOffscreenPort();
    
    /* fractalRenderJob.active is already known true (checked above), so
       this first block always runs, seeding changedRect; the loop below
       covers whatever's left of this tick's kBlocksPerIdleSlice budget. */
    DrawNextBlockAndAdvance(&changedRect);
    blocksRemaining--;
    
    while (fractalRenderJob.active && blocksRemaining-- > 0) {
        DrawNextBlockAndAdvance(&blockRect);
        UnionRect(&changedRect, &blockRect, &changedRect);
    }
    
    if (haveAccumulatedChanges)
        UnionRect(&accumulatedChangedRect, &changedRect, &accumulatedChangedRect);
    else {
        accumulatedChangedRect = changedRect;
        haveAccumulatedChanges = true;
    }
    
    if (!fractalRenderJob.active || TickCount() - lastBlitTick >= kBlitIntervalTicks) {
        EnterWindowPort();
        BlitOffscreenToWindow(&accumulatedChangedRect);
        haveAccumulatedChanges = false;
        lastBlitTick = TickCount();
    }
    
    SetPort(savedPort);
}

/* AbortFractalRender()
   Stops whatever render job is in progress, leaving the image exactly
   as refined as it currently is rather than reverting to blank -
   there's nothing to "undo" back to. Safe to call whether or not a
   render is actually running. Meant to be called when the user
   presses Command-period (see MandyWindow.c's HandleEvent()).
   
   Forces a final blit of anything AdvanceFractalRender()'s own
   throttling (kBlitIntervalTicks) had accumulated but not yet shown -
   unlike a render finishing naturally, which is always discovered
   from inside AdvanceFractalRender()'s own call stack (so its final
   blit check right after already covers it), an abort runs from a
   completely separate call path (a keypress), so nothing else would
   ever blit those last few computed blocks otherwise. */
void AbortFractalRender(void) {
    GrafPtr savedPort;
    
    if (!fractalRenderJob.active)
        return;
    
    EndRendering();
    
    if (haveAccumulatedChanges) {
        GetPort(&savedPort);
        EnterWindowPort();
        BlitOffscreenToWindow(&accumulatedChangedRect);
        haveAccumulatedChanges = false;
        SetPort(savedPort);
    }
}

/* HandleWindowResized()
   Updates windowWidth/windowHeight and imageStart to the given new
   size, then frees and reallocates the offscreen store (and the
   default-view cache and mono shade-level buffer alongside it - see
   DisposeOffscreenStore()) and starts rendering into it again from
   the coarsest pass - the same progressive mechanism used for the
   first draw, rather than a separate resize-specific redraw routine.
   This is a genuine recompute, not something the default-view cache
   (RestoreDefaultViewFromCache()) can serve: that cache is sized for
   the old dimensions, and the old offscreen content doesn't cover the
   newly exposed area at a different size regardless. (Restarting from
   scratch at the new size, rather than stretching what was already on
   screen, is the simple version of this; reusing the previous frame
   as a first guess is a possible later refinement.)
   
   Called from mwZoom.c's TrackWindowResize() after SizeWindow() has
   already resized mwWindow itself - this only updates this file's own
   size-tracking state and the offscreen store, it doesn't touch the
   window. newWidth/newHeight are trusted to already be within sensible
   bounds (TrackWindowResize() clamps against its own minimum/maximum
   before ever calling this) - the check here is just a last-resort
   guard against a degenerate (zero or negative) size reaching the
   fractal functions' sizex/sizey loops, from any future caller that
   might not clamp as carefully. */
void HandleWindowResized(short newWidth, short newHeight) {
    if (newWidth < 1 || newHeight < 1)
        return;
    
    windowWidth  = newWidth;
    windowHeight = newHeight;
    SetRect(&imageStart, 0, 0, newWidth, newHeight);
    
    DisposeOffscreenStore();
    RenderFractalOffscreen();
}

/* DrawContent()
   Paints the window's content. Ordinary update events reach this
   often - bringing the window forward, dragging another window
   across it, etc. - so it deliberately does no fractal maths: it
   just copies whatever is currently in the offscreen store onto the
   screen, complete or not, clipped by QuickDraw to the destination
   port's current visRgn/clipRgn (already narrowed to the update
   region by BeginUpdate()).
   
   If there's no offscreen store (allocation failed under low memory),
   this falls back to computing the fractal directly into the window,
   in one blocking pass, on every update.
   
   Deliberately doesn't call DrawGrowIcon(): the grow box (documentProc,
   see SetUpWindow()) is fully functional - FindWindow() recognises
   clicks in that corner as inGrow regardless of whether anything is
   drawn there - but left visually blank on purpose, rather than
   showing the standard hash-mark icon. */
void DrawContent(short active) {
    EnterWindowPort();
    
    if (offscreenReady)
        BlitOffscreenToWindow(NULL);
    else
        DrawFractalDirectly();
}
