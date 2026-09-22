# Fractal parameters

Some fractal types in Mandy take a value beyond just the view you're
looking at - a number, or a short sequence of letters - that changes
the underlying shape rather than just where you're pointing the
camera. Picking one of these from the Fractal menu opens a small
dialog for it (marked with "…", the standard Mac convention for "this
opens a dialog before doing anything"). This document is the "what do
these values actually do" reference the dialog itself doesn't have
room for.

It's organised by fractal type. See "Fixed-parameter and no-parameter
types" at the bottom for the types that don't appear above it, and
why.

## Multibrot

**Parameter:** Power (n), an integer from 2 to 8, in the dialog. The
underlying iteration and file format both tolerate a wider range (see
below) if you ever have reason to reach for it.

**What it changes:** the iteration itself, from the familiar z → z² + c
to z → zⁿ + c. Power 2 is exactly the plain Mandelbrot set - it's
allowed in the dialog mainly for completeness, since you'd normally
just pick Mandelbrot directly.

**What to actually expect as n increases**, based on the mathematics of
these sets (specifically, this is well-documented as "(n−1)-fold
rotational symmetry" - see e.g. Munafo's Mandelbrot Set Glossary and
Encyclopedia, or Milnor's work on unicritical polynomials, for the
underlying reason):

| Power | Rotational symmetry | What you'll see |
|---|---|---|
| 2 | none beyond the ordinary mirror symmetry | the familiar cardioid-and-bulbs Mandelbrot shape |
| 3 | 2-fold (180°) | two main lobes, each with 2 cusps |
| 4 | 3-fold (120°) | three main lobes arranged in a triangle, each with 3 cusps |
| 5 | 4-fold (90°) | four main lobes arranged in a cross/square, each with 4 cusps |
| 6-8 | 5- to 7-fold | progressively more lobes, arranged like the petals of a flower |

In general: **power n gives n−1 main lobes arranged symmetrically
around the centre, each lobe having n−1 cusps of its own**, and the
same n−1-fold symmetry carries through to the bulbs sprouting off each
lobe and to the corresponding Julia sets at that power.

Two other things worth knowing before you dial n up:

- **The fine filamentary detail visible at the plain Mandelbrot set's
  edges (power 2) thins out quickly as power increases** - by around
  power 5 it's largely gone, replaced by cleaner, more petal-like
  boundaries. If you're after that hairy, filament-rich texture, lower
  powers show more of it.
- **Cost scales directly with power.** Each unit of n beyond 2 adds one
  full complex multiply to every iteration, on every pixel - power 8 is
  meaningfully slower to render than power 3, especially on the 68000
  Fixed-point path. There's no shortcut available for this the way
  there is for squaring (see mwFractalMath.c's own comments on
  IterateMultibrotDouble()/Fixed()) - it's genuinely more arithmetic,
  not just an unoptimised path.

**Where to point the view:** the default view when you first switch to
Multibrot is a generic, safe Mandelbrot-like framing that shows
something recognisable at any power 2-8, but it isn't tuned for any
one of them specifically. If you zoom in and lose the main body, Zoom
Out returns you to that same default rather than wherever a previous
fractal type happened to leave the view.

**Saving:** the power you're using is written to a saved Fractal Data
file as `Power: <n>` and restored on load, the same way the view
itself is. A hand-edited file with a very large power will still load
and render rather than being rejected - but be aware that per the cost
note above, a large enough value can make a render take a very long
time, particularly on the Mac 512KE. There's a wide safety clamp (2 to
64) purely to stop a corrupted or mistyped value from making the app
appear to hang indefinitely; that clamp is not a recommendation to
actually use values anywhere near that high.

## Lyapunov

**Parameter:** a driving sequence of the letters A and B (e.g. "AB",
"AABAB", "BBAABAAB") - up to 63 characters, entered as free text in
the dialog rather than chosen from a fixed list, since there's no
small set of "the" interesting sequences the way Multibrot/Newton's
power has. Lowercase is accepted and silently converted to uppercase.

**What it changes:** Lyapunov isn't an escape-time fractal at all - it
colours by the *Lyapunov exponent* of a driven logistic map, x → r·x·(1−x),
where r alternates between two values, a and b, according to your
sequence (one character consumed per step, repeating the sequence once
it runs out - "AB" gives r = a, b, a, b, ...; "AAB" gives
a, a, b, a, a, b, ...). a and b aren't fixed constants here - they
*are* the two axes of the view, the same way c is for Mandelbrot. A
negative exponent means that particular (a, b) settles into a stable,
periodic cycle; a positive one means chaotic, sensitive-to-starting-
value behaviour. The image is a smooth gradient between the two, not
sharply banded regions - very stable and very chaotic points saturate
toward the two ends of the palette, with the interesting boundary
structure in between.

**What different sequences actually look like:** the diagonal a=b
always matches the plain, single-parameter logistic map's own
well-known bifurcation diagram, regardless of sequence - useful as a
sanity check, since it means the sequence's effect is really only
visible where a and b *differ*. Beyond that:

- **"AB"** (the single most commonly published Lyapunov fractal
  sequence) gives the classic, widely-reproduced image most people
  mean by "a Lyapunov fractal" - roughly symmetric under swapping a
  and b, since the sequence itself doesn't favour one over the other.
- **Sequences that repeat one letter more than the other** (e.g. "AAB",
  "AAAB") break that symmetry - the axis matching the more frequent
  letter dominates the resulting pattern more heavily, since the
  system spends more of each cycle following that axis's own dynamics.
- **Longer, more irregular sequences** (e.g. "AABAB", "BAAB") tend to
  produce finer, more intricate boundary structure between the stable
  and chaotic regions than a short one does, though this isn't a
  precise rule - two sequences of the same length can still look quite
  different depending on their exact pattern, and this is genuinely a
  "try it and see" parameter more than Multibrot's power is.

**Where to point the view:** the default view is a, b both ranging over
[2.5, 4.0] - the logistic map's own well-established "interesting"
region (below about 2.5 either parameter just converges to a stable
fixed point regardless of the other, and 4.0 is the map's own upper
bound). This is a real, mathematically-grounded default, not a generic
placeholder.

**Performance note:** unlike every other fractal type in this project,
Lyapunov's own maths needs `log()` inside its per-pixel calculation
itself (roughly 300 calls per pixel, not the handful of transcendental
calls other fractals only need for shading), and always runs in double
precision regardless of whether the machine has an FPU - there's no
practical Fixed-point equivalent for a transcendental function used
this heavily. Expect this to be considerably slower than the
escape-time fractals on real 68000 hardware, especially the Mac 512KE.

## Newton

**Parameter:** Power (n), an integer from 2 to 8 in the dialog (same
range as Multibrot, and same reasoning - see Multibrot's own section
above for the cost/variety tradeoff).

**What it changes:** which polynomial's roots the fractal is built
around - specifically zⁿ − 1, whose n roots are evenly spaced around
the unit circle (the n-th roots of unity). Rather than colouring by
escape speed, Newton colours by which root a starting point's own
Newton's-method iteration converges to, plus how quickly it gets
there: the image is divided into n large "basins", one per root, each
shaded by that basin's own convergence speed - a boundary region takes
many more steps to settle on a root than a point deep inside one
basin, and the interesting, fractal structure lives entirely along
those boundaries.

**What to actually expect as n increases:** more basins, arranged
symmetrically around the centre (n of them, evenly spaced, since the
roots themselves are); the boundary structure between adjacent basins
gets visually busier as there are more of them competing along shorter
shared borders. n=3 (three roots) is the single most commonly
published Newton fractal image.

**Where to point the view:** the default view is centred at the origin
with a half-width of 2 - the roots themselves always sit exactly on
the unit circle regardless of power, so this comfortably shows every
basin's own structure for any power the dialog offers.

**Performance note:** like Lyapunov, Newton always runs in double
precision regardless of gHasFPU - its own maths needs a complex
division every iteration (to compute z − f(z)/f(z)), and a correct,
fast Fixed-point division is a meaningfully bigger and riskier piece of
work than the multiplication this project's Fixed-point path already
relies on elsewhere. Expect Newton to be slower than the escape-time
fractals on real 68000 hardware, though its own iteration count is
capped much lower (32, against Julia's 300) since Newton's method
converges quadratically - most points reach a root in well under 10
steps.

## Fixed-parameter and no-parameter types

Phoenix, Barnsley Fern, and Sierpinski's triangle don't appear above
because none of them have anything to configure - no dialog, no "…" in
the menu:

- **Phoenix** uses Ushiki's own classic constant (p = −0.5) rather
  than a configurable one - it's a genuine escape-time fractal like
  Mandelbrot, just with a memory term added, and needs a per-pixel c
  the same way Mandelbrot does, so p itself was kept fixed to match
  how Julia's own constant is fixed rather than exposed.
- **Barnsley Fern** and **Sierpinski's triangle** are built by
  plotting many individual points via a fixed, well-published
  iterated-function-system recipe (Barnsley's own four affine
  transformations for the fern; the standard three-vertex "chaos game"
  for the triangle) rather than sampling a per-pixel value - there's no
  view to zoom, and nothing to configure.
