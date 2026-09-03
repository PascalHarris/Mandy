/*****
 * mwFractalMath.c
 *
 *		See mwFractalMath.h. Pure escape-time maths - no window,
 *		GWorld, or QuickDraw state anywhere in this file.
 *
 *****/
#include <math.h>
#include "mwFractalMath.h"

/* IterateEscapeTimeDouble()/IterateEscapeTimeFixed()
   z |-> z^2 + c until |z| > 2 (escape) or maxIterations. The 3-multiply
   form (zRe*zIm computed once and reused for the new zIm) is already
   the minimum multiply count for z^2+c - there's no cheaper general
   formulation to fall back to.

   Both carry a periodicity check: z is saved at doubling intervals
   (after 1, 2, 4, ... iterations) and compared against the current z
   every step; an exact match proves a cycle (this arithmetic is
   deterministic), so an interior point that would otherwise burn the
   full budget returns immediately instead. This is safe for any
   point on any escape-time fractal - it proves something about this
   point's own trajectory only, so unlike boundary-tracing schemes it
   doesn't need the set to be simply connected (this project's Julia
   set, at its fixed c, isn't).

   Fixed keeps the same 3-multiply shape (zRe*zRe, zIm*zIm, zRe*zIm)
   but gets there via FixedComputeSquaresAndCross() rather than three
   calls to the Toolbox's FixMul() - see that function's own, much
   longer comment for why: on real 68000 hardware, with no native
   32-bit multiply and no cache to soften FixMul()'s own overhead,
   that choice matters far more than anything in this comment block.
   16.16 rather than the Toolbox's higher-precision Fract (2.30) is
   still deliberate: z's intermediate values reach roughly ±6 before
   each iteration's own check catches them, comfortably inside
   Fixed's ±32767 integer range but well past Fract's ~±2. */
short IterateEscapeTimeDouble(double zRe, double zIm, double cRe, double cIm, short maxIterations) {
	short	i;
	double	savedRe = zRe;
	double	savedIm = zIm;
	short	nextSaveAt = 1;
	
	for (i = 0; i < maxIterations; i++) {
		double zReSquared = zRe * zRe;
		double zImSquared = zIm * zIm;
		
		if (zReSquared + zImSquared > 4.0)
			break;
		
		zIm = 2.0 * zRe * zIm + cIm;
		zRe = zReSquared - zImSquared + cRe;
		
		if (zRe == savedRe && zIm == savedIm) {
			i = maxIterations;
			break;
		}
		
		if (i + 1 == nextSaveAt) {
			savedRe    = zRe;
			savedIm    = zIm;
			nextSaveAt *= 2;
		}
	}
	
	return i;
}

#define kFixedFour		((Fixed) 4 << 16)
#define kFixedQuarter	(((Fixed) 1 << 16) / 4)		/* exact: 0.25 = 2^-2 */
#define kFixedSixteenth	(((Fixed) 1 << 16) / 16)		/* exact: 0.0625 = 2^-4 */

/* SplitUnsignedHalves()
   |value| as unsigned 16-bit halves (high*65536 + low == |value|,
   exactly, for any 32-bit signed value including the most negative
   one - the unsigned-subtraction form of the negation is exactly
   two's complement negation and never overflows, unlike computing
   -value directly in signed arithmetic first). Feeds
   FixedComputeSquaresAndCross()'s unsigned multiplies below - see
   its own comment for why unsigned throughout, not signed. */
static void SplitUnsignedHalves(Fixed value, unsigned short *high, unsigned short *low, Boolean *negative) {
	unsigned long magnitude = (value < 0) ? ((unsigned long) 0 - (unsigned long) value) : (unsigned long) value;
	
	*negative = (value < 0);
	*high     = (unsigned short) (magnitude >> 16);
	*low      = (unsigned short) (magnitude & 0xFFFF);
}

/* FixedComputeSquaresAndCross()
   zRe*zRe, zIm*zIm, and zRe*zIm together - exactly what
   IterateEscapeTimeFixed() needs every iteration. FixMul() would get
   there with three separate ROM trap calls, each paying its own trap
   dispatch and the ROM's own general-purpose overhead - measured at
   47 instructions with repeated memory access even on a 68020-class
   Mac (MacTech, "Fixed-Point Math", Vol 10.03) - for work this one
   ordinary function call does together, register-resident, with no
   trap at all. This is where a real 68000's own time actually goes:
   no cache to absorb FixMul()'s repeated memory access, and no native
   32-bit multiply (68020+ only) to shortcut it with either - so
   removing the trap and the redundant work is the whole of the win
   available on that hardware specifically.

   Derivation: split |A| into unsigned 16-bit halves Ah (high), Al
   (low) - Ah*65536 + Al == |A| exactly, for any 32-bit A. Then
   |A|*|B| = Ah*Bh*65536^2 + (Ah*Bl+Al*Bh)*65536 + Al*Bl, and the
   16.16 Fixed product is that divided by 65536: (Ah*Bh<<16) + Ah*Bl +
   Al*Bh + (Al*Bl>>16), sign restored afterwards from the operands'
   original signs. Every partial product is unsigned 16x16->32 - the
   68000's native MULU.W, no trap, no library call, so long as each
   multiply's operands are cast to unsigned long before the '*' (the
   standard widening-multiply idiom compilers of this vintage
   recognise and compile to one instruction rather than a generic
   32-bit multiply routine - worth confirming against Think C's own
   disassembly, since that recognition is the compiler's choice, not
   something this C code can force). Unsigned throughout, rather than
   working with the signed values directly, specifically to avoid the
   undefined behaviour of left-shifting a negative signed value in C -
   correctness matters more here than the sign-restore step costs.

   For a square (zRe*zRe, zIm*zIm - A and B are the same value), Ah*Bl
   and Al*Bh become the same product computed twice; collapsing them
   into one multiply doubled drops a square from 4 word-multiplies to
   3, and the result is never negative (a real number squared never
   is), so no sign-restore is needed there at all. zRe*zIm still needs
   the general 4-multiply form and, unlike a square, really can come
   out negative - restoring its sign needs care an early version of
   this function got wrong and a direct comparison against an exact
   64-bit reference caught: negating a magnitude that was truncated
   toward zero rounds the wrong way whenever the discarded bits are
   nonzero, versus what FixMul()/a signed arithmetic shift actually
   do, which round toward -infinity - the two differ by exactly 1 in
   that case. crossLL's own low 16 bits are exactly the true product's
   remainder mod 65536 (every other term here is already an exact
   multiple of 65536), so that's what decides whether the magnitude
   needs bumping up by one before negating. Total: 10 word-multiplies
   for all three values together, against 16 if each were a separate
   general FixMul()-equivalent multiply (4 apiece) - on top of
   removing all three trap calls. */
static void FixedComputeSquaresAndCross(Fixed zRe, Fixed zIm, Fixed *outReSquared, Fixed *outImSquared, Fixed *outCross) {
	unsigned short	reHigh, reLow, imHigh, imLow;
	Boolean			reNegative, imNegative;
	unsigned long	reCross, imCross, crossHH, crossHL, crossLH, crossLL, magnitude;
	
	SplitUnsignedHalves(zRe, &reHigh, &reLow, &reNegative);
	SplitUnsignedHalves(zIm, &imHigh, &imLow, &imNegative);
	
	reCross       = (unsigned long) reHigh * reLow;
	*outReSquared = (Fixed) ((((unsigned long) reHigh * reHigh) << 16) + (reCross << 1) + (((unsigned long) reLow * reLow) >> 16));
	
	imCross       = (unsigned long) imHigh * imLow;
	*outImSquared = (Fixed) ((((unsigned long) imHigh * imHigh) << 16) + (imCross << 1) + (((unsigned long) imLow * imLow) >> 16));
	
	crossHH = (unsigned long) reHigh * imHigh;
	crossHL = (unsigned long) reHigh * imLow;
	crossLH = (unsigned long) reLow  * imHigh;
	crossLL = (unsigned long) reLow  * imLow;
	
	magnitude = (crossHH << 16) + crossHL + crossLH + (crossLL >> 16);
	
	if (reNegative != imNegative) {
		if ((crossLL & 0xFFFF) != 0)
			magnitude += 1;		/* round the magnitude up first, so negating rounds the signed result down - see this function's own comment */
		*outCross = -(Fixed) magnitude;
	} else {
		*outCross = (Fixed) magnitude;
	}
}

short IterateEscapeTimeFixed(Fixed zRe, Fixed zIm, Fixed cRe, Fixed cIm, short maxIterations) {
	short	i;
	Fixed	savedRe = zRe;
	Fixed	savedIm = zIm;
	short	nextSaveAt = 1;
	
	for (i = 0; i < maxIterations; i++) {
		Fixed zReSquared, zImSquared, zReTimesZIm;
		
		FixedComputeSquaresAndCross(zRe, zIm, &zReSquared, &zImSquared, &zReTimesZIm);
		
		if (zReSquared + zImSquared > kFixedFour)
			break;
		
		/* "2 * zReTimesZIm" not "FixMul(zReTimesZIm, two)": doubling a
		   Fixed value's raw integer representation already doubles the
		   value it represents, so multiplying it again would just be
		   redundant, slower work for the same result. */
		zIm = 2 * zReTimesZIm + cIm;
		zRe = zReSquared - zImSquared + cRe;
		
		if (zRe == savedRe && zIm == savedIm) {
			i = maxIterations;
			break;
		}
		
		if (i + 1 == nextSaveAt) {
			savedRe    = zRe;
			savedIm    = zIm;
			nextSaveAt *= 2;
		}
	}
	
	return i;
}

/* IsInMainCardioidOrBulb()/Fixed()
   c = cRe + cIm*i lies in the main cardioid if q*(q + (cRe-0.25)) <=
   0.25*cIm^2 where q = (cRe-0.25)^2 + cIm^2; in the period-2 bulb if
   (cRe+1)^2 + cIm^2 <= 0.0625 (a circle of radius 1/4 centred at -1).
   Both are standard, independently documented results - not derived
   here. Together these two regions cover most of the set's own
   interior area, and any view framing much of the traditional "whole
   set" spends a large fraction of its pixels here, so this is worth
   checking before falling back to iteration at all. Cheaper than
   periodicity checking for exactly these two regions specifically:
   periodicity checking still runs a real iteration sequence until a
   cycle is detected, where this is a handful of multiply/compares
   with no iteration at all. It doesn't replace periodicity checking
   generally - the infinitely many smaller bulbs tangent to the
   cardioid have no simple closed form and still rely on it.

   Fixed's bounds: cRe/cIm are themselves bounded to roughly ±2 for
   any view this project's zoom limits allow, and every intermediate
   value here stays far under Fixed's ±32767 range - no overflow risk
   at any step. */
Boolean IsInMainCardioidOrBulb(double cRe, double cIm) {
	double q = (cRe - 0.25) * (cRe - 0.25) + cIm * cIm;
	
	if (q * (q + (cRe - 0.25)) <= 0.25 * cIm * cIm)
		return true;
	
	if ((cRe + 1.0) * (cRe + 1.0) + cIm * cIm <= 0.0625)
		return true;
	
	return false;
}

Boolean IsInMainCardioidOrBulbFixed(Fixed cRe, Fixed cIm) {
	Fixed cReMinusQuarter = cRe - kFixedQuarter;
	Fixed cImSquared      = FixMul(cIm, cIm);
	Fixed q               = FixMul(cReMinusQuarter, cReMinusQuarter) + cImSquared;
	
	if (FixMul(q, q + cReMinusQuarter) <= FixMul(kFixedQuarter, cImSquared))
		return true;
	
	{
		Fixed cRePlusOne = cRe + ((Fixed) 1 << 16);
		if (FixMul(cRePlusOne, cRePlusOne) + cImSquared <= kFixedSixteenth)
			return true;
	}
	
	return false;
}

/* PrepareFractalMappingDouble()/Fixed()
   See mwFractalMath.h. origin is centre minus the half-extent in
   pixels, both already scaled by step, so MapPixelToPlane*() reduces
   to origin + x*step with no further offsetting - one multiply-add
   per axis, nothing else. This is what mwWindow.c's Fixed path
   already did (PrepareFixedPointView(), now folded in here); the
   double path used to redo the full (x - width/2)/(width/2.0)*halfWidthRe
   division on every single call instead - this is the one genuinely
   new optimisation in this file, not just a house move: at the
   finest colour pass, that's one call per real pixel (up to ~150,000
   on this project's own canvas), each paying two divisions plus a
   redundant re-derivation of the aspect-corrected step, all of it
   invariant for the life of a render. Division costs meaningfully
   more than multiplication on both SANE's software float and real
   68881 hardware, so this removes real, previously-unclaimed cost
   from the majority of every render, on both the FPU and non-FPU
   paths, with no change in behaviour. */
void PrepareFractalMappingDouble(FractalMappingDouble *mapping, const FractalView *view, short pixelWidth, short pixelHeight) {
	mapping->step     = view->halfWidthRe / (pixelWidth / 2.0);
	mapping->originRe = view->centreRe - (pixelWidth  / 2) * mapping->step;
	mapping->originIm = view->centreIm - (pixelHeight / 2) * mapping->step;
}

void PrepareFractalMappingFixed(FractalMappingFixed *mapping, const FractalView *view, short pixelWidth, short pixelHeight) {
	double step = view->halfWidthRe / (pixelWidth / 2.0);
	
	mapping->step     = (Fixed) (step * 65536.0);
	mapping->originRe = (Fixed) (view->centreRe * 65536.0) - (pixelWidth  / 2) * mapping->step;
	mapping->originIm = (Fixed) (view->centreIm * 65536.0) - (pixelHeight / 2) * mapping->step;
}

void MapPixelToPlaneDouble(const FractalMappingDouble *mapping, short x, short y, double *outRe, double *outIm) {
	*outRe = mapping->originRe + x * mapping->step;
	*outIm = mapping->originIm + y * mapping->step;
}

void MapPixelToPlaneFixed(const FractalMappingFixed *mapping, short x, short y, Fixed *outRe, Fixed *outIm) {
	*outRe = mapping->originRe + x * mapping->step;
	*outIm = mapping->originIm + y * mapping->step;
}

/* ShadeLevelForIterationCount()
   Escape times are heavily skewed toward small counts - most exterior
   points escape almost immediately - so mapping them onto the shade
   range linearly spends nearly all of it on counts almost no pixel
   reaches, leaving the rest indistinguishable from the background.
   The log curve spreads colour across the counts pixels actually
   land in. Each fractal normalises against its own maxIterations
   rather than being rescaled onto another fractal's scale first, so
   this one function replaces what used to be separate direct
   (Mandelbrot) and rescaled (Julia) linear mappings.

   kLogPlusOneTable[]/EnsureLogTableReady(): this needs log(n+1) for
   two different n each call - iterationCount (genuinely different
   every call) and maxIterations (always one of a handful of values
   across this whole project, recomputing the *same* result every
   call). log() is transcendental; on an FPU-less Mac it runs through
   SANE's software float library, meaningfully slower than +/-/*//
   - and unlike the escape-time shortcuts above, every single sample
   needs a shade level regardless of what's being viewed. Precomputing
   log(i+1) for every i any caller could plausibly ask for turns both
   calls into plain array reads, built once, lazily, on first use.
   float, not double: the result only ever feeds a 0..kShadingScale
   level, so float's precision is already far more than enough, and
   it halves the table's size for no loss that matters.

   kFractalMaxIterationsSupported governs the table's size rather than
   any one fractal's own ceiling, deliberately: this module doesn't
   know about Mandelbrot's or Julia's specific constants, so it can't
   size itself against kJuliaMaxIterations without pulling that
   coupling back in. 512 gives comfortable headroom over every
   maxIterations this project defines today, at a fixed, small cost
   (2KB) regardless of window size. Contract: no caller may pass an
   iterationCount or maxIterations above this. */
#define kFractalMaxIterationsSupported	512

static float	kLogPlusOneTable[kFractalMaxIterationsSupported + 1];
static Boolean	gLogTableReady = false;

static void EnsureLogTableReady(void) {
	short i;
	
	if (gLogTableReady)
		return;
	
	for (i = 0; i <= kFractalMaxIterationsSupported; i++)
		kLogPlusOneTable[i] = (float) log((double) i + 1.0);
	
	gLogTableReady = true;
}

short ShadeLevelForIterationCount(short iterationCount, short maxIterations) {
	double shadeLevel;
	
	EnsureLogTableReady();
	
	shadeLevel = kShadingScale * (double) kLogPlusOneTable[iterationCount] / (double) kLogPlusOneTable[maxIterations];
	
	if (shadeLevel > kShadingScale)
		shadeLevel = kShadingScale;
	
	return (short) shadeLevel;
}
