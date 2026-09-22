/*****
 * mwNewtonMath.h
 *
 *		Newton's method fractal maths - convergence toward a root, not
 *		escape-time, statistical, or direct-draw (see mwFractalMath.h/
 *		mwLyapunovMath.h's own scopes), so kept in its own file for the
 *		same reason those two are separate from each other.
 *
 *****/
#ifndef _mwNewtonMath_
#define _mwNewtonMath_

/* Runs Newton's method on z^power - 1 from the given starting point,
   for up to maxIterations steps or until successive iterates converge
   (see the .c file for the exact criterion), whichever comes first.
   Returns the iteration count actually taken (for shading - faster
   convergence shades differently from slower within the same root's
   band, see SampleNewton() in mwWindow.c), and writes the final z
   through outRe/outIm so the caller can determine which of the
   polynomial's power roots it landed nearest - see NewtonRootIndex().
   power must be at least 2. Always double - see the .c file's own
   comment on why, the same reasoning IterateLyapunovExponent() already
   applies. */
short IterateNewton(double startRe, double startIm, short power, short maxIterations, double *outRe, double *outIm);

/* Which of z^power - 1's own power roots (the power-th roots of unity)
   the given point is nearest - 0..power-1. Meant to be called on
   IterateNewton()'s own output; a separate function rather than folded
   into it, since a caller sampling many pixels at the same power can
   reuse this without IterateNewton() needing to know anything about
   root positions itself. */
short NewtonRootIndex(double zRe, double zIm, short power);

#endif	/* _mwNewtonMath_ */
