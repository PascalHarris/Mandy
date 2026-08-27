/*****
 * mwWindow.c
 *
 *		The window routines for the Mandy Fractal Generator
 *
 *****/
#include <math.h>
#include <stdio.h>
#include <QDOffscreen.h>
#include "mwWindow.h"
#ifndef _Quickdraw_
#include <Quickdraw.h>
#endif

extern	Boolean	gHasColorQD;	/* set once in MandyWindow.c's InitMacintosh() */

#define windowX 0
#define windowY 40
#define windowWidth 512
#define windowHeight 300
#define pi 3.14159265

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
   checks. Worth retuning once this can be timed on real hardware. */
#define kBlockGridTargetColumns	4
#define kBlocksPerIdleSlice		4

/* Escape-time fractal parameters. kShadingScale is the common range
   both SampleMandelbrot() and SampleJulia() report on, so ShadeBlock()
   can use one fixed set of thresholds (monochrome) or one fixed colour
   ramp (colour) regardless of which fractal's own maxIterations
   produced the value. */
#define kShadingScale			64

#define kMandelbrotZoom			150.0
#define kMandelbrotMaxIterations	64

#define kJuliaZoom			1.0
#define kJuliaOffsetX			0.0
#define kJuliaOffsetY			0.0
#define kJuliaConstantRe		-0.7
#define kJuliaConstantIm		0.27015
#define kJuliaMaxIterations		300

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
Rect		windowBounds = { windowY, windowX, windowY+windowHeight, windowX+windowWidth };
Rect		imageStart = {0, 0, windowHeight, windowWidth};
int			width = 5; 

/* Offscreen pixel store --------------------------------------------
   The progressive renderer draws into this buffer; DrawContent() then
   just copies finished pixels onto the screen. Two different
   technologies back it depending on gHasColorQD:
   
   - Monochrome: a plain BitMap with a manually allocated
     baseAddr/rowBytes, wrapped in an ordinary GrafPort. This is the
     classic pre-Color QuickDraw offscreen-bitmap technique, so it
     works unmodified on real Mac Plus hardware.
   - Colour: an 8-bit indexed GWorld with a small custom colour table
     (see BuildFractalColorTable()) built to hold a smooth ramp across
     kShadingScale. 8-bit indexed, rather than matching the screen's
     actual depth, is deliberate: CopyBits() automatically dithers
     this down to whatever the real screen supports (4-bit and up),
     and an indexed image is what a future palette-cycling animation
     (the "trippy" effect on the roadmap) needs to rewrite cheaply.
   
   Only one of offscreenPort/offscreenBits or offscreenGWorld is ever
   live at a time, selected by gHasColorQD; offscreenBounds and
   offscreenReady describe whichever one is current. */
static GrafPort		offscreenPort;
static BitMap		offscreenBits;
static GWorldPtr	offscreenGWorld;
static Rect			offscreenBounds;
static Boolean		offscreenReady = false;

/* A fractal sample function reports how "escaped" the point at (x,y)
   is, on the shared kShadingScale range - see SampleMandelbrot() and
   SampleJulia(). */
typedef short (*FractalSampleProc)(short x, short y);


/* Progressive render job -------------------------------------------
   Tracks an in-progress coarse-to-fine render so AdvanceFractalRender()
   can pick up where it left off each time it's called. There is only
   ever one job at a time; starting a new one (RenderFractalOffscreen())
   simply overwrites whatever was in progress. */
static struct {
	Boolean				active;
	FractalSampleProc	sampleProc;
	short				blockSize;
	short				columnCount;
	short				rowCount;
	short				nextColumn;
	short				nextRow;
} fractalRenderJob;

static void			BeginRendering(void);
static void			EndRendering(void);
static short		IterateEscapeTime(double zRe, double zIm, double cRe, double cIm, short maxIterations);
static short		SampleMandelbrot(short x, short y);
static short		SampleJulia(short x, short y);
static short		ShadeLevelForIterationCount(short iterationCount, short maxIterations);
static RGBColor		ColorForShadeLevel(short shadeLevel);
static unsigned short	InterpolateComponent(unsigned short from, unsigned short to, double fraction);
static CTabHandle	BuildFractalColorTable(short entryCount);
static short		CurrentFinestBlockSize(void);
static short		CurrentScreenDepth(void);
static Boolean		ShouldRenderInColor(void);
static void			ShadeBlock(const Rect *blockRect, short shadeLevel);
static void			FillIndexedRect(const Rect *blockRect, short shadeLevel);
static void			DrawFractalDirectly(void);
static short		BlocksAcross(short span, short blockSize);
static short		HighestPowerOfTwoAtMost(short n);
static Boolean		AllocateOffscreenMonoStore(void);
static Boolean		AllocateOffscreenColorStore(void);
static Boolean		AllocateOffscreenStore(void);
static void			DisposeOffscreenStore(void);
static void			StartProgressiveRender(FractalSampleProc sampleProc);
static void			DrawNextBlockAndAdvance(void);
static void			AdvanceToNextBlock(void);
static void			BeginNextPass(void);
static void			EnterOffscreenPort(void);
static void			EnterWindowPort(void);
static void			BlitOffscreenToWindow(void);

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
    
    if (gHasColorQD)
        mwWindow = NewCWindow(0L, &windowBounds, kIdleWindowTitle, true, noGrowDocProc, (WindowPtr) -1L, true, 0);
    else
        mwWindow = NewWindow(0L, &windowBounds, kIdleWindowTitle, true, noGrowDocProc, (WindowPtr) -1L, true, 0);
    
    SetPort(mwWindow);
    
    RenderFractalOffscreen();
}

void DrawBranch(float x1, float y1, float angle, float depth) {
	if (depth != 0) {
		float x2 = x1 + cos(angle*(pi/180.0))*depth*10;
		float y2 = y1 + sin(angle*(pi/180.0))*depth*10;
		
		MoveTo(x1,windowHeight-y1);
		Line(x2-x1,y1-y2);
		
		DrawBranch(x2,y2,angle-20,depth-1);
		DrawBranch(x2,y2,angle+20,depth-1);
	
	}

}

/* IterateEscapeTime()
   The z = z^2 + c iteration shared by the Mandelbrot and Julia sets -
   they differ only in which of z's or c's starting value is the point
   being tested and which is fixed. Returns the number of iterations
   completed before |z| escaped past 2, or maxIterations if it never
   did. */
static short IterateEscapeTime(double zRe, double zIm, double cRe, double cIm, short maxIterations) {
	short i;
	
	for (i = 0; i < maxIterations; i++) {
		double zReSquared = zRe * zRe;
		double zImSquared = zIm * zIm;
		
		if (zReSquared + zImSquared > 4.0)
			break;
		
		zIm = 2.0 * zRe * zIm + cIm;
		zRe = zReSquared - zImSquared + cRe;
	}
	
	return i;
}

/* SampleMandelbrot()
   The point tested is c = (x,y); z starts at the origin. The image is
   symmetric about the vertical centre, so y is folded to a distance
   from the centre line rather than drawn twice as the original code
   did. */
static short SampleMandelbrot(short x, short y) {
	short verticalDistanceFromCentre = y - windowHeight/2;
	double cRe, cIm;
	short  iterationCount;
	
	if (verticalDistanceFromCentre < 0)
		verticalDistanceFromCentre = -verticalDistanceFromCentre;
	
	cRe = (double) x / kMandelbrotZoom - 2.0;
	cIm = (double) verticalDistanceFromCentre / kMandelbrotZoom;
	
	iterationCount = IterateEscapeTime(0.0, 0.0, cRe, cIm, kMandelbrotMaxIterations);
	
	return ShadeLevelForIterationCount(iterationCount, kMandelbrotMaxIterations);
}

/* SampleJulia()
   The point tested is z's starting value; c is the fixed constant that
   shapes the Julia set. */
static short SampleJulia(short x, short y) {
	double zRe = 1.5 * (x - windowWidth/2)  / (0.5 * kJuliaZoom * windowWidth)  + kJuliaOffsetX;
	double zIm =       (y - windowHeight/2) / (0.5 * kJuliaZoom * windowHeight) + kJuliaOffsetY;
	short  iterationCount = IterateEscapeTime(zRe, zIm, kJuliaConstantRe, kJuliaConstantIm, kJuliaMaxIterations);
	
	return ShadeLevelForIterationCount(iterationCount, kJuliaMaxIterations);
}

/* ShadeLevelForIterationCount()
   Maps a fractal's raw iteration count onto the shared kShadingScale
   range using a log curve rather than a straight linear one. Escape
   times are heavily skewed toward small counts - most exterior points
   escape almost immediately - so a linear map spends nearly its whole
   range on iteration counts almost no pixel ever reaches, leaving the
   overwhelming majority of the exterior indistinguishable from the
   erased white background. The log curve spreads colour across the
   counts pixels actually land in instead. Each fractal is mapped
   against its own maxIterations, rather than being rescaled onto
   another fractal's scale first, so this one function replaces both
   the old direct (Mandelbrot) and rescaled (Julia) linear mappings. */
static short ShadeLevelForIterationCount(short iterationCount, short maxIterations) {
	double shadeLevel = kShadingScale * log((double) iterationCount + 1.0) / log((double) maxIterations + 1.0);
	
	if (shadeLevel > kShadingScale)
		shadeLevel = kShadingScale;
	
	return (short) shadeLevel;
}

/* The colour ramp shadeLevel is mapped onto, in the same direction as
   the monochrome buckets below: 0 (fast escape) is light,
   kShadingScale (slow escape, or never) is dark. The specific stops -
   white through yellow/orange/red-purple to black - are an arbitrary
   starting aesthetic, easy to change; they're also exactly what a
   future palette-cycling animation would rewrite. */
typedef struct {
	short		shadeLevel;
	RGBColor	color;
} ColorRampStop;

static const ColorRampStop kColorRamp[] = {
	{ 0,                        { 65535, 65535, 65535 } },	/* white  */
	{ kShadingScale / 4,        { 65535, 65535, 0     } },	/* yellow */
	{ kShadingScale / 2,        { 65535, 16384, 0     } },	/* orange */
	{ (kShadingScale * 3) / 4,  { 32768, 0,     16384 } },	/* red-purple */
	{ kShadingScale,            { 0,     0,     0     } }	/* black  */
};
#define kColorRampStopCount 5

/* InterpolateComponent()
   Linear blend of one RGBColor component between two ramp stops. */
static unsigned short InterpolateComponent(unsigned short from, unsigned short to, double fraction) {
	return (unsigned short) (from + (to - from) * fraction);
}

/* ColorForShadeLevel()
   Finds the pair of ramp stops shadeLevel falls between and linearly
   blends their colours. */
static RGBColor ColorForShadeLevel(short shadeLevel) {
	short i;
	
	for (i = 1; i < kColorRampStopCount; i++) {
		if (shadeLevel <= kColorRamp[i].shadeLevel) {
			short  rangeStart = kColorRamp[i-1].shadeLevel;
			short  rangeEnd   = kColorRamp[i].shadeLevel;
			double fraction   = (rangeEnd > rangeStart) ? (double) (shadeLevel - rangeStart) / (rangeEnd - rangeStart) : 0.0;
			RGBColor result;
			
			result.red   = InterpolateComponent(kColorRamp[i-1].color.red,   kColorRamp[i].color.red,   fraction);
			result.green = InterpolateComponent(kColorRamp[i-1].color.green, kColorRamp[i].color.green, fraction);
			result.blue  = InterpolateComponent(kColorRamp[i-1].color.blue,  kColorRamp[i].color.blue,  fraction);
			return result;
		}
	}
	
	return kColorRamp[kColorRampStopCount - 1].color;
}

/* BuildFractalColorTable()
   Hand-builds a ColorTable of entryCount entries (a Handle sized for
   ColorTable's trailing variable-length ctTable array), one per
   possible shadeLevel, so the offscreen GWorld's CLUT is our own
   fractal ramp rather than the system default. The caller owns the
   returned handle; NewGWorld() copies what it needs from it rather
   than keeping it, so it should be disposed (via DisposeCTable())
   once passed to NewGWorld(). Returns NULL on low memory. */
static CTabHandle BuildFractalColorTable(short entryCount) {
	long		tableSize  = sizeof(ColorTable) + (long) (entryCount - 1) * sizeof(ColorSpec);
	CTabHandle	colorTable = (CTabHandle) NewHandle(tableSize);
	short		i;
	
	if (colorTable == NULL)
		return NULL;
	
	(**colorTable).ctSeed  = GetCTSeed();
	(**colorTable).ctFlags = 0;
	(**colorTable).ctSize  = entryCount - 1;
	
	for (i = 0; i < entryCount; i++) {
		(**colorTable).ctTable[i].value = i;
		(**colorTable).ctTable[i].rgb   = ColorForShadeLevel(i);
	}
	
	return colorTable;
}

/* CurrentScreenDepth()
   The main screen's current pixel depth in bits (1, 2, 4, 8, 16, or
   32). Checked fresh each time rather than cached, since it's cheap
   (a couple of field reads, no searching) and it means a depth change
   made mid-session via the Monitors control panel is picked up on the
   next render rather than needing a relaunch. Assumes a single
   display, matching the simplification already made elsewhere for
   this app's fixed small window. */
static short CurrentScreenDepth(void) {
	GDHandle		mainDevice       = GetMainDevice();
	PixMapHandle	mainDevicePixMap = (**mainDevice).gdPMap;
	
	return (**mainDevicePixMap).pixelSize;
}

/* ShouldRenderInColor()
   Colour QuickDraw being present isn't by itself a reason to draw in
   colour: at 1-bit and 2-bit depths the render should look exactly
   like a genuine black-and-white Mac, with no attempt at colour at
   all, rather than colour that then gets dithered down to almost
   nothing meaningful. This is the single place that decision is made;
   ShadeBlock() and CurrentFinestBlockSize() both defer to it instead
   of checking gHasColorQD directly. */
static Boolean ShouldRenderInColor(void) {
	return gHasColorQD && (CurrentScreenDepth() >= 4);
}

/* CurrentFinestBlockSize()
   Colour refines all the way to real 1x1 pixels. Monochrome - which
   now includes 1-bit and 2-bit colour screens, not just genuinely
   monochrome ones, see ShouldRenderInColor() - stops one level short,
   at 2x2, leaving room for a dither pattern to simulate colour at the
   finest visible unit - the same 2x2 granularity the original
   hand-written Mandelbrot()/Julia() sampled at, now generalised to
   every block size via ShadeBlock(). */
static short CurrentFinestBlockSize(void) {
	return ShouldRenderInColor() ? 1 : 2;
}

/* ShadeBlock()
   Colours a block according to how far up the shared kShadingScale its
   sample fell. In monochrome, that's one of QuickDraw's standard
   dither patterns via FillRect(), unchanged from before this file
   supported colour. In colour, it's a direct pixel-memory write via
   FillIndexedRect() rather than any QuickDraw colour-setting call:
   shadeLevel already *is* the correct index into our own colour table
   (BuildFractalColorTable() constructs it that way on purpose), so
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
   device dependency, no Palette Manager dependency, nothing to link. */
static void ShadeBlock(const Rect *blockRect, short shadeLevel) {
	if (ShouldRenderInColor()) {
		FillIndexedRect(blockRect, shadeLevel);
		return;
	}
	
	if (shadeLevel > 32)
		FillRect(blockRect, black);
	else if (shadeLevel > 24)
		FillRect(blockRect, dkGray);
	else if (shadeLevel > 12)
		FillRect(blockRect, gray);
	else if (shadeLevel > 6)
		FillRect(blockRect, ltGray);
	else
		FillRect(blockRect, white);
}

/* FillIndexedRect()
   Writes shadeLevel directly into every pixel byte of blockRect in
   the offscreen GWorld's own pixel memory - only ever called once
   ShouldRenderInColor() is true, so the GWorld (8 bits per pixel, one
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

/* DrawFractalDirectly()
   Draws the whole image into the current port in one pass at full
   resolution, with no progress shown along the way. This is only used
   when there's no offscreen store to render into (see DrawContent()) -
   a rare, already-degraded situation where keeping the fallback simple
   matters more than keeping it responsive. Works in colour or
   monochrome exactly like the progressive path, since it shares
   ShadeBlock() and just steps by CurrentFinestBlockSize() instead of
   working through a job. */
static void DrawFractalDirectly(void) {
	EraseRect(&imageStart);
	
	if (width == 1) {
		DrawBranch(windowWidth/2, 0, 90, 9);
	} else if (width == 2 || width == 3) {
		FractalSampleProc sampleProc = (width == 2) ? SampleMandelbrot : SampleJulia;
		short step = CurrentFinestBlockSize();
		short x, y;
		
		for (y = 0; y < windowHeight; y += step) {
			for (x = 0; x < windowWidth; x += step) {
				Rect cell;
				SetRect(&cell, x, y, x + step, y + step);
				ShadeBlock(&cell, sampleProc(x + step/2, y + step/2));
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

/* AllocateOffscreenMonoStore()
   Manually allocates a plain BitMap the size of imageStart and wraps
   it in a GrafPort so QuickDraw can target it directly - the classic
   pre-Color QuickDraw offscreen-bitmap technique, unchanged from
   before this file supported colour. */
static Boolean AllocateOffscreenMonoStore(void) {
    short	storeWidth  = imageStart.right  - imageStart.left;
    short	storeHeight = imageStart.bottom - imageStart.top;
    long	storeRowBytes = ((long) (storeWidth + 15) / 16) * 2;
    Ptr		storeBaseAddr = NewPtr(storeRowBytes * (long) storeHeight);
    
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
    
    return true;
}

/* AllocateOffscreenColorStore()
   Creates an 8-bit indexed GWorld the size of imageStart, using our
   own fractal colour ramp rather than the system's default CLUT. The
   pixels are locked for the GWorld's whole lifetime (see
   DisposeOffscreenStore()) rather than around each individual draw,
   since it's small (well under 512K's headroom even though colour
   Macs never actually run this tight on memory) and locking once is
   one less thing to get wrong at every call site. */
static Boolean AllocateOffscreenColorStore(void) {
	CTabHandle	fractalColors = BuildFractalColorTable(kShadingScale + 1);
	QDErr		error;
	
	if (fractalColors == NULL)
		return false;
	
	error = NewGWorld(&offscreenGWorld, 8, &imageStart, fractalColors, NULL, 0);
	DisposeCTable(fractalColors);	/* NewGWorld() copies what it needs, per Inside Mac - see chat */
	
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
	if (!(gHasColorQD ? AllocateOffscreenColorStore() : AllocateOffscreenMonoStore()))
		return false;
	
	offscreenBounds = imageStart;
	offscreenReady  = true;
	return true;
}

/* DisposeOffscreenStore()
   Frees whichever offscreen store is currently allocated so it can be
   reallocated at a new size (see HandleWindowResized()). Safe to call
   when nothing is currently allocated. */
static void DisposeOffscreenStore(void) {
    if (!offscreenReady)
        return;
    
    if (gHasColorQD) {
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
	
	GetPort(&savedPort);
	SetPort(mwWindow);
	SetWTitle(mwWindow, kRenderingWindowTitle);
	SetPort(savedPort);
}

static void EndRendering(void) {
	GrafPtr savedPort;
	
	fractalRenderJob.active = false;
	
	GetPort(&savedPort);
	SetPort(mwWindow);
	SetWTitle(mwWindow, kIdleWindowTitle);
	SetPort(savedPort);
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
	
	fractalRenderJob.columnCount = BlocksAcross(imageWidth,  fractalRenderJob.blockSize);
	fractalRenderJob.rowCount    = BlocksAcross(imageHeight, fractalRenderJob.blockSize);
	fractalRenderJob.nextColumn  = 0;
	fractalRenderJob.nextRow     = 0;
	BeginRendering();
}

/* DrawNextBlockAndAdvance()
   Samples and shades the single next block in the job, then moves the
   job on to the following block (or the next pass, or completion).
   Assumes the offscreen port is already current. */
static void DrawNextBlockAndAdvance(void) {
	Rect	blockRect, clippedRect;
	short	sampleX, sampleY;
	short	left = fractalRenderJob.nextColumn * fractalRenderJob.blockSize;
	short	top  = fractalRenderJob.nextRow    * fractalRenderJob.blockSize;
	
	SetRect(&blockRect, left, top, left + fractalRenderJob.blockSize, top + fractalRenderJob.blockSize);
	SectRect(&blockRect, &offscreenBounds, &clippedRect);
	
	sampleX = clippedRect.left + (clippedRect.right  - clippedRect.left) / 2;
	sampleY = clippedRect.top  + (clippedRect.bottom - clippedRect.top)  / 2;
	
	ShadeBlock(&clippedRect, fractalRenderJob.sampleProc(sampleX, sampleY));
	
	AdvanceToNextBlock();
}

/* AdvanceToNextBlock()
   Moves the job to the next column, wrapping to the next row, and on
   to the next (finer) pass once a whole row/column grid is done. */
static void AdvanceToNextBlock(void) {
	if (++fractalRenderJob.nextColumn < fractalRenderJob.columnCount)
		return;
	
	fractalRenderJob.nextColumn = 0;
	if (++fractalRenderJob.nextRow < fractalRenderJob.rowCount)
		return;
	
	BeginNextPass();
}

/* BeginNextPass()
   Halves the block size and rebuilds the grid for the next, finer
   pass - or marks the job finished once blocks are already as fine as
   CurrentFinestBlockSize() allows. */
static void BeginNextPass(void) {
	if (fractalRenderJob.blockSize <= CurrentFinestBlockSize()) {
		EndRendering();
		return;
	}
	
	fractalRenderJob.blockSize  /= 2;
	fractalRenderJob.columnCount = BlocksAcross(offscreenBounds.right  - offscreenBounds.left, fractalRenderJob.blockSize);
	fractalRenderJob.rowCount    = BlocksAcross(offscreenBounds.bottom - offscreenBounds.top,  fractalRenderJob.blockSize);
	fractalRenderJob.nextColumn  = 0;
	fractalRenderJob.nextRow     = 0;
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
	SetPort(gHasColorQD ? (GrafPtr) offscreenGWorld : &offscreenPort);
}

static void EnterWindowPort(void) {
	SetPort(mwWindow);
}

/* BlitOffscreenToWindow()
   Copies the offscreen store onto the window. Assumes the caller has
   already called EnterWindowPort() (both DrawContent() and
   AdvanceFractalRender() do this themselves, since each needs the
   port/device set for its own reasons too).
   
   &mwWindow->portBits works as the destination in both colour and
   monochrome without an explicit branch: mwWindow's C type has always
   been WindowPtr (= GrafPtr), and Inside Macintosh documents coercing
   a CGrafPtr to a GrafPtr and reading .portBits as the correct,
   intentional way to get a CopyBits-compatible pointer from a colour
   port - QuickDraw recognises it's really a PixMap by the high bits
   left set at that offset. The source needs the explicit branch since
   it's one of two genuinely different backing stores. */
static void BlitOffscreenToWindow(void) {
    BitMap *sourceBits = gHasColorQD ? &((GrafPtr) offscreenGWorld)->portBits : &offscreenBits;
    
    if (!((WindowPeek) mwWindow)->visible)
        return;
    
    CopyBits(sourceBits, &mwWindow->portBits, &offscreenBounds, &imageStart, srcCopy, NULL);
}

/* RenderFractalOffscreen()
   Starts (or restarts) rendering into the offscreen store: the tree is
   cheap enough to just draw outright, but Mandelbrot and Julia only
   have their first, coarsest pass queued up here - AdvanceFractalRender()
   does the actual work, a little at a time, from HandleEvent()'s idle
   time, so the image comes into focus instead of the app looking
   hung. Call this only when the fractal's parameters actually change:
   the fractal type (HandleMenu()'s fractalID case), a window resize
   (HandleWindowResized()), or a future zoom - never from an ordinary
   update event.
   
   If the offscreen store isn't available and can't be allocated (low
   memory), this does nothing; DrawContent() then falls back to
   recomputing the fractal directly into the window on every update. */
void RenderFractalOffscreen(void) {
    GrafPtr	savedPort;
    
    GetPort(&savedPort);
    
    if (!offscreenReady && !AllocateOffscreenStore()) {
        EndRendering();
        SetPort(savedPort);
        return;
    }
    
    EnterOffscreenPort();
    EraseRect(&offscreenBounds);
    
	if (width == 1) {
		DrawBranch(windowWidth/2, 0, 90, 9);
		EndRendering();
	} else if (width == 2) {
		StartProgressiveRender(SampleMandelbrot);
	} else if (width == 3) {
		StartProgressiveRender(SampleJulia);
	} else {
		EndRendering();
	}
    
    SetPort(savedPort);
}

/* AdvanceFractalRender()
   Draws up to kBlocksPerIdleSlice more blocks of whatever render job
   is in progress, then shows the progress so far on screen. Meant to
   be called from the main event loop's idle time (see
   MandyWindow.c's HandleEvent()); does nothing if there's no job
   running. */
void AdvanceFractalRender(void) {
    GrafPtr	savedPort;
    short	blocksRemaining = kBlocksPerIdleSlice;
    
    if (!fractalRenderJob.active || !offscreenReady)
        return;
    
    GetPort(&savedPort);
    
    EnterOffscreenPort();
    while (fractalRenderJob.active && blocksRemaining-- > 0)
        DrawNextBlockAndAdvance();
    
    EnterWindowPort();
    BlitOffscreenToWindow();
    
    SetPort(savedPort);
}

/* AbortFractalRender()
   Stops whatever render job is in progress, leaving the image exactly
   as refined as it currently is rather than reverting to blank -
   there's nothing to "undo" back to. Safe to call whether or not a
   render is actually running. Meant to be called when the user
   presses Command-period (see MandyWindow.c's HandleEvent()). */
void AbortFractalRender(void) {
    if (!fractalRenderJob.active)
        return;
    
    EndRendering();
}

/* HandleWindowResized()
   Frees and reallocates the offscreen store at the window's new size,
   then starts rendering into it again from the coarsest pass - the
   same progressive mechanism used for the first draw, rather than a
   separate resize-specific redraw routine. Nothing calls this yet -
   the window has no grow box today - but it's ready for the
   resizing/zoom work (#5) to call once imageStart reflects the new
   size. (Restarting from scratch at the new size, rather than
   stretching what was already on screen, is the simple version of
   this; reusing the previous frame as a first guess is a possible
   later refinement.) */
void HandleWindowResized(void) {
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
   in one blocking pass, on every update. */
void DrawContent(short active) {
    EnterWindowPort();
    
    if (offscreenReady)
        BlitOffscreenToWindow();
    else
        DrawFractalDirectly();
}
