/*****
 * mwWindow.h
 *
 *		Public interfaces for mwWindow.c
 *
 *****/

void DrawContent (short active);
void SetUpWindow(void);
void RenderFractalOffscreen(void);
void AdvanceFractalRender(void);
void AbortFractalRender(void);
void HandleWindowResized(void);

/* Read-only access to render state, for the Get Info window (mwInfo.c). */
Boolean IsRenderActive(void);
unsigned long RenderElapsedTicks(void);
ConstStr255Param CurrentFractalName(void);
short CurrentScreenDepth(void);
void GetFractalResolution(short *outWidth, short *outHeight);

/* The mathematical constants behind whichever fractal is currently
   selected (width, extern'd directly elsewhere in this project - see
   mwMenus.c/mwInfo.c - decides which fields are meaningful):
   maxIterations and zoom apply to Mandelbrot and Julia both;
   constantRe/constantIm are Julia's fixed c and are meaningless for
   Mandelbrot (c varies per pixel there, so there's no single value to
   show) or the tree (not an escape-time fractal at all). Fields not
   meaningful for the current fractal are set to 0. */
typedef struct {
	double	zoom;
	short	maxIterations;
	double	constantRe;
	double	constantIm;
} FractalParameters;

FractalParameters GetFractalParameters(void);

/* Read-only access to the offscreen image, for Save As (mwSaveAs.c) as
   well as this file's own DrawContent()/AdvanceFractalRender(). Returns
   false (leaving *bits/*bounds untouched) if there's nothing to read -
   the offscreen store failed to allocate under low memory. */
Boolean GetOffscreenImage(BitMap **bits, Rect *bounds);
