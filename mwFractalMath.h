/*****
 * mwFractalMath.h
 *
 *		Shared escape-time fractal maths: iteration, the interior/
 *		symmetry shortcuts that let SampleMandelbrot()/SampleJulia()
 *		(mwWindow.c) skip iteration altogether, view-to-pixel mapping,
 *		and log-scaled shading. Nothing here touches QuickDraw, a
 *		GWorld, or window state, so any future z^2+c-family fractal
 *		can reuse it unchanged - not just Mandelbrot/Julia.
 *
 *****/
#ifndef _mwFractalMath_
#define _mwFractalMath_

#ifndef _Quickdraw_
#include <Quickdraw.h>	/* Boolean - see mwWindow.c's own include order, which this matches */
#endif
#ifndef _FixMath_
#include <FixMath.h>	/* Fixed, FixMul */
#endif
#include "mwWindow.h"	/* FractalView */

/* Escape-time iteration - the two representations mwWindow.c's
   gHasFPU flag dispatches between. Returns the iteration at which
   |z| exceeded 2 (escape), or maxIterations (ran out of budget, or a
   detected cycle - see the .c file's periodicity check). Deliberately
   two separate functions rather than one generic one: double and
   Fixed aren't unifiable in ANSI C without macro-generated code,
   which would cost more in readability and step-debuggability on
   this toolchain than the ~25 lines of duplication saves - a DRY
   violation, flagged here rather than silently accepted. */
short IterateEscapeTimeDouble(double zRe, double zIm, double cRe, double cIm, short maxIterations);
short IterateEscapeTimeFixed(Fixed zRe, Fixed zIm, Fixed cRe, Fixed cIm, short maxIterations);

/* Closed-form interior tests for the Mandelbrot set's main cardioid
   and period-2 bulb - skip iteration entirely for a c already known
   never to escape. Mandelbrot-specific: c varies per pixel there, so
   "is this c interior" is a meaningful per-pixel question. Not valid
   for Julia (c is the fixed constant, not the point under test). */
Boolean IsInMainCardioidOrBulb(double cRe, double cIm);
Boolean IsInMainCardioidOrBulbFixed(Fixed cRe, Fixed cIm);

/* Precision floors for ClampHalfWidthRe() (mwWindow.c) - how far
   either representation can actually resolve adjacent pixels before
   IterateEscapeTimeDouble()/Fixed() can no longer tell them apart.
   Double: ~15-16 significant decimal digits, order-1 coordinates,
   ~512px width - floor chosen with headroom, not exact derivation.
   Fixed: 16.16 has an *absolute*, not relative, resolution of
   1/65536 regardless of magnitude - the per-pixel step must clear
   that with real margin, giving a floor two orders of magnitude
   shallower. This is the real cost of the Fixed path's speed: deep
   zooms need a real FPU. */
#define kFractalMinHalfWidthReDouble	0.0001
#define kFractalMinHalfWidthReFixed	0.05

/* One-time-per-render view-to-pixel mapping. Precomputing this turns
   the per-pixel path into a single multiply-add on each axis, with
   no division and no repeated re-derivation of the aspect-corrected
   step - stepRe and stepIm are always equal (pixels are square), so
   only one step value is kept. origin is the plane point at pixel
   (0,0), already folded together with the half-width/height offset
   so MapPixelToPlane*() needs nothing but x, y, and this struct. */
typedef struct {
	double	originRe, originIm, step;
} FractalMappingDouble;

typedef struct {
	Fixed	originRe, originIm, step;
} FractalMappingFixed;

void PrepareFractalMappingDouble(FractalMappingDouble *mapping, const FractalView *view, short pixelWidth, short pixelHeight);
void PrepareFractalMappingFixed (FractalMappingFixed  *mapping, const FractalView *view, short pixelWidth, short pixelHeight);

void MapPixelToPlaneDouble(const FractalMappingDouble *mapping, short x, short y, double *outRe, double *outIm);
void MapPixelToPlaneFixed (const FractalMappingFixed  *mapping, short x, short y, Fixed *outRe, Fixed *outIm);

/* Iteration-count -> normalised 0..kShadingScale shade level, log-
   scaled so exterior detail (heavily skewed toward small counts)
   doesn't collapse into a handful of visible bands. Always
   normalises against maxIterations, the fractal's true ceiling, even
   when the caller iterated against a smaller, reduced ceiling for a
   coarse preview pass (see mwWindow.c's UpdateIterationCeilingForBlockSize())
   - real testing showed normalising against the reduced ceiling
   instead makes colours shift visibly pass to pass. */
#define kShadingScale	64

short ShadeLevelForIterationCount(short iterationCount, short maxIterations);

#endif	/* _mwFractalMath_ */
