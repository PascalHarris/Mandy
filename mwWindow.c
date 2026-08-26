/*****
 * mwWindow.c
 *
 *		The window routines for the Mandy Fractal Generator
 *
 *****/
#include <math.h>
#include <stdio.h>
#include "mwWindow.h"
#ifndef _Quickdraw_
#include <Quickdraw.h>
#endif

#define windowX 0
#define windowY 40
#define windowWidth 512
#define windowHeight 300
#define pi 3.14159265

/* Progressive-render tuning -------------------------------------------
   kBlockGridTargetColumns: the coarsest pass aims for about this many
   blocks across the longer side of the image (rounded down to a power
   of two), which is what gives a 512-wide window 4 columns.
   kFinestBlockSize: the pass at which refinement stops - matches the
   2x2 granularity the original code sampled at.
   kBlocksPerIdleSlice: how many blocks AdvanceFractalRender() draws
   before yielding back to the event loop. Smaller keeps the app
   checking for input more often (smoother, more responsive); larger
   finishes a render sooner but leaves longer gaps between input
   checks. Worth retuning once this can be timed on real hardware. */
#define kBlockGridTargetColumns	4
#define kFinestBlockSize		2
#define kBlocksPerIdleSlice		4

/* Escape-time fractal parameters. kShadingScale is the common range
   both SampleMandelbrot() and SampleJulia() report on, so ShadeBlock()
   can use one fixed set of thresholds regardless of which fractal's
   own maxIterations produced the value. */
#define kShadingScale			64

#define kMandelbrotZoom			150.0
#define kMandelbrotMaxIterations	64

#define kJuliaZoom			1.0
#define kJuliaOffsetX			0.0
#define kJuliaOffsetY			0.0
#define kJuliaConstantRe		-0.7
#define kJuliaConstantIm		0.27015
#define kJuliaMaxIterations		300

WindowPtr	mwWindow;
Rect		dragRect;
Rect		windowBounds = { windowY, windowX, windowY+windowHeight, windowX+windowWidth };
Rect		imageStart = {0, 0, windowHeight, windowWidth};
int			width = 5; 

/* Offscreen pixel store --------------------------------------------
   The progressive renderer draws into this buffer; DrawContent() then
   just copies finished pixels onto the screen. This is the classic
   (pre-Color QuickDraw) offscreen-bitmap technique - a plain BitMap
   with a manually allocated baseAddr/rowBytes, wrapped in an ordinary
   GrafPort - so it works unmodified on the original 512K Mac too,
   though the progressive renderer below is really aimed at making a
   Mac Plus-class machine feel responsive rather than a 512K one.
   A GWorld-based equivalent for colour Macs is separate work. */
static GrafPort	offscreenPort;
static BitMap	offscreenBits;
static Rect		offscreenBounds;
static Boolean	offscreenReady = false;

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

static Boolean	AllocateOffscreenStore(void);
static void		DisposeOffscreenStore(void);
static short	IterateEscapeTime(double zRe, double zIm, double cRe, double cIm, short maxIterations);
static short	SampleMandelbrot(short x, short y);
static short	SampleJulia(short x, short y);
static void		ShadeBlock(const Rect *blockRect, short shadeLevel);
static void		DrawFractalDirectly(void);
static short	BlocksAcross(short span, short blockSize);
static short	HighestPowerOfTwoAtMost(short n);
static void		StartProgressiveRender(FractalSampleProc sampleProc);
static void		DrawNextBlockAndAdvance(void);
static void		AdvanceToNextBlock(void);
static void		BeginNextPass(void);
static void		BlitOffscreenToWindow(void);

/* SetUpWindow()
   Create the Minimum Window window, and open it. */
void SetUpWindow(void) {
    dragRect = screenBits.bounds;
    
    mwWindow = NewWindow(0L, &windowBounds, "\pFractal Window", true, noGrowDocProc, (WindowPtr) -1L, true, 0);
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
   did. Already reports on the kShadingScale range, since
   kMandelbrotMaxIterations equals it. */
static short SampleMandelbrot(short x, short y) {
	short verticalDistanceFromCentre = y - windowHeight/2;
	double cRe, cIm;
	
	if (verticalDistanceFromCentre < 0)
		verticalDistanceFromCentre = -verticalDistanceFromCentre;
	
	cRe = (double) x / kMandelbrotZoom - 2.0;
	cIm = (double) verticalDistanceFromCentre / kMandelbrotZoom;
	
	return IterateEscapeTime(0.0, 0.0, cRe, cIm, kMandelbrotMaxIterations);
}

/* SampleJulia()
   The point tested is z's starting value; c is the fixed constant that
   shapes the Julia set. Rescaled from kJuliaMaxIterations down onto
   kShadingScale so ShadeBlock() doesn't need to know it's much larger
   than Mandelbrot's. */
static short SampleJulia(short x, short y) {
	double zRe = 1.5 * (x - windowWidth/2)  / (0.5 * kJuliaZoom * windowWidth)  + kJuliaOffsetX;
	double zIm =       (y - windowHeight/2) / (0.5 * kJuliaZoom * windowHeight) + kJuliaOffsetY;
	short  iterationCount = IterateEscapeTime(zRe, zIm, kJuliaConstantRe, kJuliaConstantIm, kJuliaMaxIterations);
	
	return (short) (((long) iterationCount * kShadingScale) / kJuliaMaxIterations);
}

/* ShadeBlock()
   Fills a block with one of QuickDraw's standard dither patterns
   according to how far up the shared kShadingScale its sample fell.
   These patterns tile correctly across arbitrary pixel boundaries, so
   this same call works whether blockRect is a whole coarse-pass block
   or a single finest-pass cell - one shading routine for every
   resolution the progressive renderer draws at. */
static void ShadeBlock(const Rect *blockRect, short shadeLevel) {
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

/* DrawFractalDirectly()
   Draws the whole image into the current port in one pass at full
   resolution, with no progress shown along the way. This is only used
   when there's no offscreen store to render into (see DrawContent()) -
   a rare, already-degraded situation where keeping the fallback simple
   matters more than keeping it responsive. */
static void DrawFractalDirectly(void) {
	EraseRect(&imageStart);
	
	if (width == 1) {
		DrawBranch(windowWidth/2, 0, 90, 9);
	} else if (width == 2 || width == 3) {
		FractalSampleProc sampleProc = (width == 2) ? SampleMandelbrot : SampleJulia;
		short x, y;
		
		for (y = 0; y < windowHeight; y += kFinestBlockSize) {
			for (x = 0; x < windowWidth; x += kFinestBlockSize) {
				Rect cell;
				SetRect(&cell, x, y, x + kFinestBlockSize, y + kFinestBlockSize);
				ShadeBlock(&cell, sampleProc(x + kFinestBlockSize/2, y + kFinestBlockSize/2));
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
   starting block size that's clean to halve down to kFinestBlockSize. */
static short HighestPowerOfTwoAtMost(short n) {
	short powerOfTwo = 1;
	
	while (powerOfTwo * 2 <= n)
		powerOfTwo *= 2;
	
	return powerOfTwo;
}

/* AllocateOffscreenStore()
   Manually allocates a plain (non-colour) BitMap the size of
   imageStart and wraps it in a GrafPort so QuickDraw can target it
   directly. Returns false if there isn't enough memory to allocate it;
   callers must be able to cope with that by drawing straight to the
   window instead. */
static Boolean AllocateOffscreenStore(void) {
    short	storeWidth  = imageStart.right  - imageStart.left;
    short	storeHeight = imageStart.bottom - imageStart.top;
    long	storeRowBytes = ((long) (storeWidth + 15) / 16) * 2;
    Ptr		storeBaseAddr = NewPtr(storeRowBytes * (long) storeHeight);
    
    if (storeBaseAddr == NULL)
        return false;
    
    offscreenBounds = imageStart;
    offscreenBits.baseAddr = storeBaseAddr;
    offscreenBits.rowBytes = storeRowBytes;
    offscreenBits.bounds   = offscreenBounds;
    
    OpenPort(&offscreenPort);
    SetPort(&offscreenPort);
    SetPortBits(&offscreenBits);
    offscreenPort.portRect = offscreenBounds;
    RectRgn(offscreenPort.visRgn, &offscreenBounds);
    ClipRect(&offscreenBounds);
    
    offscreenReady = true;
    return true;
}

/* DisposeOffscreenStore()
   Frees the offscreen buffer so it can be reallocated at a new size
   (see HandleWindowResized()). Safe to call when nothing is
   currently allocated. */
static void DisposeOffscreenStore(void) {
    if (!offscreenReady)
        return;
    
    ClosePort(&offscreenPort);
    DisposePtr(offscreenBits.baseAddr);
    offscreenBits.baseAddr = NULL;
    offscreenReady = false;
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
	
	fractalRenderJob.sampleProc = sampleProc;
	fractalRenderJob.blockSize  = HighestPowerOfTwoAtMost(longerSide / kBlockGridTargetColumns);
	if (fractalRenderJob.blockSize < kFinestBlockSize)
		fractalRenderJob.blockSize = kFinestBlockSize;
	
	fractalRenderJob.columnCount = BlocksAcross(imageWidth,  fractalRenderJob.blockSize);
	fractalRenderJob.rowCount    = BlocksAcross(imageHeight, fractalRenderJob.blockSize);
	fractalRenderJob.nextColumn  = 0;
	fractalRenderJob.nextRow     = 0;
	fractalRenderJob.active      = true;
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
   pass - or marks the job finished once blocks are already at
   kFinestBlockSize. */
static void BeginNextPass(void) {
	if (fractalRenderJob.blockSize <= kFinestBlockSize) {
		fractalRenderJob.active = false;
		return;
	}
	
	fractalRenderJob.blockSize  /= 2;
	fractalRenderJob.columnCount = BlocksAcross(offscreenBounds.right  - offscreenBounds.left, fractalRenderJob.blockSize);
	fractalRenderJob.rowCount    = BlocksAcross(offscreenBounds.bottom - offscreenBounds.top,  fractalRenderJob.blockSize);
	fractalRenderJob.nextColumn  = 0;
	fractalRenderJob.nextRow     = 0;
}

/* BlitOffscreenToWindow()
   Copies the offscreen store onto the window. Assumes the caller has
   already made mwWindow the current port (both DrawContent() and
   AdvanceFractalRender() do this themselves, since each needs the port
   set for its own reasons too). */
static void BlitOffscreenToWindow(void) {
    if (!((WindowPeek) mwWindow)->visible)
        return;
    
    CopyBits(&offscreenBits, &mwWindow->portBits, &offscreenBounds, &imageStart, srcCopy, NULL);
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
        SetPort(savedPort);
        return;
    }
    
    SetPort(&offscreenPort);
    EraseRect(&offscreenBounds);
    
	if (width == 1) {
		DrawBranch(windowWidth/2, 0, 90, 9);
		fractalRenderJob.active = false;
	} else if (width == 2) {
		StartProgressiveRender(SampleMandelbrot);
	} else if (width == 3) {
		StartProgressiveRender(SampleJulia);
	} else {
		fractalRenderJob.active = false;
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
    
    SetPort(&offscreenPort);
    while (fractalRenderJob.active && blocksRemaining-- > 0)
        DrawNextBlockAndAdvance();
    
    SetPort(mwWindow);
    BlitOffscreenToWindow();
    
    SetPort(savedPort);
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
    SetPort(mwWindow);
    
    if (offscreenReady)
        BlitOffscreenToWindow();
    else
        DrawFractalDirectly();
}
