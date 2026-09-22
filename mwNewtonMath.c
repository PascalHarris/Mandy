/*****
 * mwNewtonMath.c
 *
 *		See mwNewtonMath.h.
 *
 *****/
#include <math.h>
#include "mwNewtonMath.h"

#define kNewtonConvergenceEpsilon	1e-6

/* IterateNewton()
   See mwNewtonMath.h. Newton's method: z_new = z - f(z)/f'(z),
   f(z) = z^power - 1, f'(z) = power*z^(power-1). z^(power-1) is built
   up first via repeated multiplication (the same technique
   IterateMultibrotDouble(), mwFractalMath.c, uses for its own z^power -
   duplicated rather than shared, since that function is scoped to the
   escape-time family and this is a convergence algorithm, not an
   escape-time one; sharing would mean either exposing escape-time-
   specific internals to a caller outside that family or moving the
   shared part somewhere else for two current call sites, more
   machinery than the loop in question justifies), then z^power is one
   further complex multiply of that by z - cheaper than computing both
   powers via two separate repeated-multiplication loops, which would
   redo almost all the same work twice.

   f'(z) can be exactly zero only at z=0 (the polynomial's own critical
   point) - checked explicitly before dividing, rather than after a
   divide-by-zero has already happened, since IEEE division by an
   exact zero produces infinity, not a trapped error, and that infinity
   would silently poison every following iteration rather than failing
   visibly. A point that hits this is reported as not having converged
   (returns maxIterations, with outRe/outIm left at z=0) -
   NewtonRootIndex() would call z=0 "closest to" whichever root happens
   to sit nearest the origin, which isn't meaningful, so SampleNewton()
   (mwWindow.c) treats maxIterations as its own "did not converge" case
   rather than asking for a root at all.

   Complex division (f(z)/f'(z)) uses the standard conjugate-multiply
   technique: multiplying both the numerator and denominator by the
   denominator's own complex conjugate makes the new denominator real
   (a^2+b^2), leaving an ordinary real division either part can then
   use directly. Verified against a range of starting points and both
   power 3 and power 4 (checking convergence to the correct nearest
   root, correct fixed-point behaviour when started exactly on a root,
   and correct non-convergence at the z=0 critical point) before
   relying on it here.

   Always double, regardless of gHasFPU - complex division needs a
   reciprocal (an actual division, not just multiplies), and building
   a Fixed-point division correctly is meaningfully riskier than the
   multiplication work FixedMultiply() (mwFractalMath.c) already went
   through - worth doing carefully as dedicated work later if this
   path's own speed on non-FPU hardware turns out to matter, not worth
   the risk of rushing alongside several other new fractal types at
   once. The honest cost: like Lyapunov, Newton is slower on real 68000
   hardware than this project's escape-time fractals, which need no
   division at all in their own inner loop. */
short IterateNewton(double startRe, double startIm, short power, short maxIterations, double *outRe, double *outIm) {
	double	zRe = startRe, zIm = startIm;
	short	i;
	
	for (i = 0; i < maxIterations; i++) {
		double	prevPowerRe = zRe, prevPowerIm = zIm;		/* will become z^(power-1) */
		double	powerRe, powerIm;							/* z^power */
		double	fRe, fIm, fPrimeRe, fPrimeIm, denominator, newRe, newIm;
		short	k;
		
		if (zRe == 0.0 && zIm == 0.0) {
			*outRe = 0.0;
			*outIm = 0.0;
			return maxIterations;
		}
		
		for (k = 1; k < power - 1; k++) {
			double newPowerRe = prevPowerRe * zRe - prevPowerIm * zIm;
			double newPowerIm = prevPowerRe * zIm + prevPowerIm * zRe;
			prevPowerRe = newPowerRe;
			prevPowerIm = newPowerIm;
		}
		
		powerRe = prevPowerRe * zRe - prevPowerIm * zIm;
		powerIm = prevPowerRe * zIm + prevPowerIm * zRe;
		
		fRe = powerRe - 1.0;
		fIm = powerIm;
		fPrimeRe = (double) power * prevPowerRe;
		fPrimeIm = (double) power * prevPowerIm;
		
		denominator = fPrimeRe * fPrimeRe + fPrimeIm * fPrimeIm;
		
		newRe = zRe - (fRe * fPrimeRe + fIm * fPrimeIm) / denominator;
		newIm = zIm - (fIm * fPrimeRe - fRe * fPrimeIm) / denominator;
		
		if (fabs(newRe - zRe) < kNewtonConvergenceEpsilon && fabs(newIm - zIm) < kNewtonConvergenceEpsilon) {
			zRe = newRe;
			zIm = newIm;
			i++;
			break;
		}
		
		zRe = newRe;
		zIm = newIm;
	}
	
	*outRe = zRe;
	*outIm = zIm;
	return i;
}

/* NewtonRootIndex()
   See mwNewtonMath.h. The power-th roots of unity for whichever power
   was last asked for are cached (gCachedRootRe/Im, gCachedPower) and
   only recomputed when power actually changes - cos()/sin() are each
   called power times per recomputation, not per pixel, since a whole
   render asks this the same power every time. Recomputing them fresh
   per pixel would mean millions of transcendental calls across a full
   image on top of IterateNewton()'s own division-per-iteration cost,
   for values that never actually change within one render.

   kNewtonMaxCachedPower matches mwWindow.c's own SetNewtonPower()
   clamp ceiling exactly, not just the dialog's own suggested 2..8
   range - a hand-edited .frct file can still ask for anything up to
   that clamp, and a mismatch here would silently stop checking roots
   beyond this array's size rather than fail visibly, exactly the kind
   of quiet wrong-answer bug this project's own testing has caught
   before in other guises. */
#define kNewtonMaxCachedPower	64

static double gCachedRootRe[kNewtonMaxCachedPower];
static double gCachedRootIm[kNewtonMaxCachedPower];
static short  gCachedPower = 0;

short NewtonRootIndex(double zRe, double zIm, short power) {
	short	k;
	short	bestIndex = 0;
	double	bestDistance = 0.0;
	
	if (power != gCachedPower) {
		for (k = 0; k < power && k < kNewtonMaxCachedPower; k++) {
			double angle = 2.0 * 3.14159265358979323846 * (double) k / (double) power;
			gCachedRootRe[k] = cos(angle);
			gCachedRootIm[k] = sin(angle);
		}
		gCachedPower = power;
	}
	
	for (k = 0; k < power && k < kNewtonMaxCachedPower; k++) {
		double dRe = zRe - gCachedRootRe[k];
		double dIm = zIm - gCachedRootIm[k];
		double distance = dRe * dRe + dIm * dIm;
		
		if (k == 0 || distance < bestDistance) {
			bestDistance = distance;
			bestIndex = k;
		}
	}
	
	return bestIndex;
}
