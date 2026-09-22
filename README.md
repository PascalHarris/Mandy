# Mandy

A fractal generator for classic 68k Macintosh computers (since 2016).

## What it does

Mandy renders several kinds of image into a window, chosen from the
**Fractal** menu, grouped there by family:

- **Tree** — a recursive branching tree, drawn directly with QuickDraw lines.
- **Barnsley Fern** — a fixed iterated-function-system recipe (four
  affine transformations, applied with fixed probabilities), plotted
  point by point.
- **Sierpinski** — Sierpinski's triangle, via the standard three-vertex
  "chaos game" construction.
- **Mandelbrot** — the Mandelbrot set.
- **Julia** — a Julia set at a fixed constant.
- **Burning Ship** — the Mandelbrot iteration with both of z's parts
  folded onto the positive axes before squaring, every iteration.
- **Tricorn** (Mandelbar) — the Mandelbrot iteration against z's complex
  conjugate.
- **Multibrot…** — z raised to a chosen power (rather than squared) plus
  c; opens a dialog to choose the power.
- **Phoenix** — the Mandelbrot iteration with a memory term feeding
  back the previous iterate as well as the current one (Ushiki, 1988).
- **Lyapunov…** — not escape-time at all: colours a driven logistic
  map by its own Lyapunov exponent (stable vs. chaotic), driven by a
  sequence of A's and B's entered in a dialog.
- **Newton…** — Newton's-method convergence toward one of zⁿ − 1's own
  roots, coloured by which root and how fast; opens a dialog to choose
  the power (how many roots).

See `FractalParameters.md` for what each configurable value (marked
with "…" in the menu) actually does.

Every sampled fractal type (everything above except Tree, Barnsley
Fern, and Sierpinski, which draw directly instead) renders
progressively: the image starts at a coarse block size and refines
through several passes down to full resolution, with each pass
visiting blocks in a recursive quadrant order rather than a plain
top-to-bottom scan, so the picture comes into focus as a whole rather
than sweeping like a scanline. Rendering happens a little at a time on
idle events, so the application stays responsive while a render is in
progress. Command-Period aborts a render early, leaving whatever has
been drawn so far on screen.

Other features:

- **Get Info** a small floating window showing the current fractal's
  resolution, screen depth, render time, and mathematical parameters
  (zoom, iteration count where meaningful, Julia's constant) - which
  of these apply varies by fractal type.
- **Save As...** exports the current image as a PICT file.
- **Animate / Stop Animation** cycles the displayed colours or
  patterns without recomputing the fractal. On colour Macs this rotates
  the image's own colour table. 

The application automatically detects whether Color QuickDraw is
available and renders in colour where possible, falling back to
QuickDraw's standard dither patterns (white/light gray/gray/dark
gray/black) on black-and-white screens or at 1-bit/2-bit screen depths.

## Requirements

- **Classic Mac OS**, System 6 or System 7.
- **68k Macintosh hardware**. The minimum
  target is a **Mac Plus with 1MB of RAM**..
- Works on both black-and-white and colour Macs. Colour Macs need
  Color QuickDraw.
- The window is a fixed 512×300 — there's no resizing or zoom yet.

## Building

MandyWindow is written in classic Mac Toolbox C — plain C against
QuickDraw, the Window Manager, and the Menu Manager, not Objective-C,
Carbon, or Cocoa. It's built with **Think C**; no other toolchain has
been used or tested against it, though it should port to other 68k Mac
Toolbox C environments (MPW, CodeWarrior) without much difficulty.

No third-party libraries are required — only the classic Mac Toolbox
headers (`Quickdraw.h` and friends) and the standard ANSI C library
that ships with Think C (`<stdio.h>`, `<math.h>`).

### Source files

| File | Purpose |
|---|---|
| `MandyWindow.c` | Application entry point and main event loop |
| `mwWindow.c` / `.h` | Offscreen buffering, window management, progressive-render scheduling, the fractal type registry |
| `mwFractalMath.c` / `.h` | Escape-time iteration, interior/precision shortcuts, pixel-to-plane mapping, log-scaled shading - shared maths with no window or QuickDraw state, reusable for any z^2+c-family fractal |
| `mwLyapunovMath.c` / `.h` | The Lyapunov fractal's own maths - kept separate from mwFractalMath since it isn't escape-time |
| `mwNewtonMath.c` / `.h` | Newton's-method fractal maths - also kept separate, since it's convergence-based rather than escape-time |
| `mwParameterDialog.c` / `.h` | Reusable "enter one or more values" dialog for any fractal type needing runtime configuration (Multibrot's and Newton's power, Lyapunov's driving sequence) - requires a DLOG/DITL resource pair at ID 131, documented at the top of the .c file |
| `mwMenus.c` / `.h` | Menu bar setup and menu command dispatch |
| `mwInfo.c` / `.h` | The Get Info palette window |
| `mwSaveAs.c` / `.h` | Save As... (PICT export) and Save/Load Fractal Data |
| `mwColorCycle.c` / `.h` | The Animate / Stop Animation feature |
| `FractalParameters.md` | What each fractal type's own configurable value actually does, for anyone using (or extending) the parameter dialog |

### Known constraints

A few Toolbox routines were deliberately avoided after testing showed
they don't work in this project's build/link setup, in case that trips
up future changes:

- `SetGWorld` reliably crashes on real hardware here — ports are
  switched with plain `SetPort` instead.
- `PmForeColor` and `CTabChanged` (Palette Manager / Color QuickDraw
  routines) fail to link — colour is set via direct pixel-memory writes
  and colour-table manipulation instead of these calls.
