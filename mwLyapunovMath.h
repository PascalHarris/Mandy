/*****
 * mwLyapunovMath.h
 *
 *		The Lyapunov fractal's own maths - not escape-time at all (see
 *		mwFractalMath.h's own scope), so kept out of that file rather
 *		than crowding a module explicitly named and documented for the
 *		escape-time family with an algorithm that isn't one.
 *
 *****/
#ifndef _mwLyapunovMath_
#define _mwLyapunovMath_

/* The Lyapunov exponent for the driven logistic map x -> r*x*(1-x),
   with r cycling through sequence (a string of 'A'/'B' characters,
   sequenceLength long, repeating from the start once exhausted) as a
   and b themselves range over the view - see the .c file for the full
   derivation, the fixed warm-up/averaging iteration counts, and why
   this is always double regardless of gHasFPU. Returns a real number,
   not an iteration count - negative means the sequence settles into a
   stable cycle, positive means chaotic behaviour; SampleLyapunov()
   (mwWindow.c) turns that into a shade level. sequence must contain
   only the characters 'A' and 'B' and sequenceLength must be at least
   1 - the caller's own job to validate (see ConfigureLyapunov()),
   since this function has no sensible fallback for a malformed
   sequence to default to. */
double IterateLyapunovExponent(double a, double b, const char *sequence, short sequenceLength);

#endif	/* _mwLyapunovMath_ */
