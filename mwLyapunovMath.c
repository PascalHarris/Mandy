/*****
 * mwLyapunovMath.c
 *
 *		See mwLyapunovMath.h.
 *
 *****/
#include <math.h>
#include "mwLyapunovMath.h"

/* kLyapunovWarmupIterations/kLyapunovAverageIterations/kLyapunovInitialX
   See IterateLyapunovExponent()'s own comment for the reasoning behind
   each. Fixed, not configurable - tuned for a reasonable per-pixel
   cost against a reasonably stable exponent estimate, not derived from
   a formal convergence criterion. */
#define kLyapunovWarmupIterations	100
#define kLyapunovAverageIterations	200
#define kLyapunovInitialX			0.5

/* IterateLyapunovExponent()
   See mwLyapunovMath.h. The Lyapunov fractal's own construction
   (Markus & Hess, 1989): not an escape-time count at all, but a real
   number - the Lyapunov exponent of a logistic map x -> r*x*(1-x)
   whose growth rate r alternates between a and b according to
   sequence (one character consumed per iteration, wrapping back to
   the start once exhausted - "AB" gives r = a,b,a,b,...; "AAB" gives
   a,a,b,a,a,b,...). Negative exponents mean the sequence settles into
   a stable cycle; positive means chaotic, sensitive-to-initial-
   conditions behaviour - see SampleLyapunov() (mwWindow.c) for how the
   sign and magnitude become a shade level.

   x always starts at 0.5, the logistic map's own critical point - the
   standard choice (see e.g. the Lyapunov fractal's own Wikipedia
   article), not an arbitrary one: every convergent cycle attracts at
   least one critical point, so starting there is what lets a short run
   reliably find whichever cycle actually exists, rather than depending
   on a less representative starting value.

   kLyapunovWarmupIterations are run and discarded before any exponent
   contribution is accumulated, so the average isn't skewed by the
   initial transient before the trajectory has actually settled onto
   its long-run behaviour; kLyapunovAverageIterations are then averaged
   (of ln|r*(1-2x)|, the logistic map's own derivative magnitude at
   each step) to produce the returned exponent.

   x leaving (0,1), or the derivative landing on exactly zero (which
   happens whenever x is exactly 0.5, the map's own critical point -
   guaranteed on the very first warm-up step, since x starts there, and
   possible again later for a sequence whose cycle revisits it) are
   both guarded explicitly: the first would otherwise feed a bad value
   into log() below it entirely, the second would make a single step
   contribute log(0) = negative infinity, permanently poisoning the
   running average. Neither is a bug in the maths - a Lyapunov exponent
   genuinely can be unboundedly negative at exactly this point - but an
   unguarded infinity in one pixel's result would be a visible artefact
   in the shaded image, not a meaningful "more stable than everywhere
   else" signal worth keeping.

   Always double, regardless of gHasFPU - unlike every fractal in
   mwFractalMath.c, the Lyapunov exponent itself requires log(), not
   just this project's own log-scaled shading (ShadeLevelForIterationCount()
   already needs that on every fractal, escape-time or not - this is a
   second, unrelated log() requirement, inside the per-pixel maths
   itself). There's no reasonable Fixed-point equivalent for a
   transcendental function in the hot loop the way there is for the
   escape-time family's pure multiply-add work, and building a lookup-
   table approximation accurate enough across the wide range
   |r*(1-2x)| can take here would be a substantial, separately-risky
   undertaking for a fractal whose whole point is a two-tone stability
   map, not fine numerical precision. The honest cost of this choice:
   Lyapunov is considerably slower on real 68000 hardware than this
   project's other fractals, running every log() through SANE's
   software floating point rather than a real FPU - accepted
   deliberately here, the same way this project already accepts that
   Julia's 300-iteration ceiling costs more than Mandelbrot's 64. */
double IterateLyapunovExponent(double a, double b, const char *sequence, short sequenceLength) {
	double	x = kLyapunovInitialX;
	double	exponentSum = 0.0;
	short	step = 0;
	short	i;
	
	for (i = 0; i < kLyapunovWarmupIterations; i++) {
		double r = (sequence[step] == 'A') ? a : b;
		
		x = r * x * (1.0 - x);
		
		step++;
		if (step >= sequenceLength)
			step = 0;
		
		if (x <= 0.0 || x >= 1.0)
			return 1.0;		/* left the map's own valid range - as chaotic/degenerate a result as this fractal can report, rather than feeding a bad x into the averaging phase below */
	}
	
	for (i = 0; i < kLyapunovAverageIterations; i++) {
		double r          = (sequence[step] == 'A') ? a : b;
		double derivative = r * (1.0 - 2.0 * x);
		double magnitude  = fabs(derivative);
		
		if (magnitude < 1e-10)
			magnitude = 1e-10;		/* floor rather than let log() see exactly zero - see this function's own comment */
		
		x = r * x * (1.0 - x);
		
		step++;
		if (step >= sequenceLength)
			step = 0;
		
		if (x <= 0.0 || x >= 1.0)
			return 1.0;
		
		exponentSum += log(magnitude);
	}
	
	return exponentSum / (double) kLyapunovAverageIterations;
}
