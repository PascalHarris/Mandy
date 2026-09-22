/*****
 * mwInfo.c
 *
 *	The "Get Info" window: a small palette-style window showing the
 *	current fractal's resolution, screen bit depth, render time (or
 *	elapsed time, while a render is still in progress), and its
 *	underlying mathematical parameters.
 *
 *****/
#include "mwInfo.h"
#include "mwWindow.h"
#include <math.h>
#include <stdio.h>
#ifndef _Quickdraw_
#include <Quickdraw.h>
#endif

extern	int	width;	/* which fractal is selected - see mwWindow.c */

#define kInfoWindowWidth	230
#define kInfoWindowHeight	140
#define kInfoWindowLeft		60
#define kInfoWindowTop		60
#define kInfoWindowTitle	"\pFractal Info"

#define kInfoFontSize		9

#define kLeftMargin			12
#define kTopMargin			10

#define kCloseButtonTitle	"\pClose"
#define kCloseButtonWidth	70
#define kCloseButtonHeight	20
#define kCloseButtonBottomMargin	12

/* movableDBoxProc gives a compact window with a short, draggable
   title bar and no zoom/grow boxes - the closest built-in
   approximation to a "palette" style window available without
   shipping a custom WDEF resource, which a genuinely Finder-Get-Info-
   style thin palette title bar would need. Always a plain NewWindow(),
   never NewCWindow(), regardless of gHasColourQD: this window only
   ever draws plain black text and one button, so there's nothing here
   that benefits from being a colour window, and one less place to
   carry that complexity. */
#define kInfoWindowProcID	movableDBoxProc

#define kRefreshIntervalTicks	60	/* about once a second, while a render is active */

static WindowPtr		gInfoWindow = NULL;
static ControlHandle	gCloseButton = NULL;
static unsigned long	gLastRefreshTick = 0;
static Boolean			gWasRenderActiveLastCheck = false;

static void	AppendPascalString(Str255 dst, ConstStr255Param src);
static void	AppendCString(Str255 dst, const char *src);
static void	AppendNumber(Str255 dst, long n);
static void	DrawInfoLine(short lineNumber, ConstStr255Param text, short firstBaseline, short lineHeight);
static void	BuildFractalLine(Str255 line);
static void	BuildResolutionLine(Str255 line);
static void	BuildDepthLine(Str255 line);
static void	BuildTimeLine(Str255 line);
static void	BuildZoomLine(Str255 line, const FractalParameters *params);
static void	BuildMaxIterationsLine(Str255 line, const FractalParameters *params);
static void	BuildConstantLine(Str255 line, const FractalParameters *params);

/* AppendPascalString()
   Appends src onto dst, truncating rather than overflowing if dst is
   already close to Str255's 255-byte cap - never actually reached by
   anything this file builds, but cheap insurance regardless. */
static void AppendPascalString(Str255 dst, ConstStr255Param src) {
	short spaceLeft  = 255 - dst[0];
	short copyLength = (src[0] < spaceLeft) ? src[0] : spaceLeft;
	short i;
	
	for (i = 1; i <= copyLength; i++)
		dst[dst[0] + i] = src[i];
	
	dst[0] += copyLength;
}

/* AppendCString()
   Same as AppendPascalString(), but for a NUL-terminated C string -
   what sprintf() produces, used below for the floating-point
   parameter lines NumToString() can't format. */
static void AppendCString(Str255 dst, const char *src) {
	short spaceLeft = 255 - dst[0];
	short i = 0;
	
	while (src[i] != '\0' && i < spaceLeft) {
		dst[dst[0] + i + 1] = src[i];
		i++;
	}
	
	dst[0] += i;
}

/* AppendNumber()
   Appends the decimal representation of a whole number onto dst. */
static void AppendNumber(Str255 dst, long n) {
	Str255 numberString;
	
	NumToString(n, numberString);
	AppendPascalString(dst, numberString);
}

/* DrawInfoLine()
   Draws one line of text at a vertical position determined only by
   lineNumber (0-based), firstBaseline (where line 0's baseline sits),
   and lineHeight (the current font's real line spacing - see
   DrawInfoWindowContent()) - so each Build*Line() function can be
   drawn independently without tracking a running position itself. */
static void DrawInfoLine(short lineNumber, ConstStr255Param text, short firstBaseline, short lineHeight) {
	MoveTo(kLeftMargin, firstBaseline + lineNumber * lineHeight);
	DrawString(text);
}

/* BuildFractalLine()
   "Fractal: <name>", or "(none selected)" via CurrentFractalName()
   before anything's been chosen from the Fractal menu yet. */
static void BuildFractalLine(Str255 line) {
	line[0] = 0;
	AppendPascalString(line, "\pFractal: ");
	AppendPascalString(line, CurrentFractalName());
}

/* BuildResolutionLine()
   "Resolution: <width> x <height>". */
static void BuildResolutionLine(Str255 line) {
	short imageWidth, imageHeight;
	
	GetFractalResolution(&imageWidth, &imageHeight);
	
	line[0] = 0;
	AppendPascalString(line, "\pResolution: ");
	AppendNumber(line, imageWidth);
	AppendPascalString(line, "\p x ");
	AppendNumber(line, imageHeight);
}

/* BuildDepthLine()
   "Screen depth: <n>-bit" - the real screen's current depth (see
   CurrentScreenDepth() in mwWindow.c), which is what actually decides
   whether the fractal renders in colour or, at 1-bit/2-bit, in the
   same dither patterns as a genuine black-and-white Mac. */
static void BuildDepthLine(Str255 line) {
	line[0] = 0;
	AppendPascalString(line, "\pScreen depth: ");
	AppendNumber(line, CurrentScreenDepth());
	AppendPascalString(line, "\p-bit");
}

/* BuildTimeLine()
   "Rendering... <n> sec elapsed" while a render is active, or
   "Render time: <n> sec" once it's stopped - whether that's by
   finishing normally or being aborted; RenderElapsedTicks() doesn't
   distinguish those two, and neither does this line. */
static void BuildTimeLine(Str255 line) {
	unsigned long elapsedSeconds = RenderElapsedTicks() / 60;
	
	line[0] = 0;
	
	if (IsRenderActive()) {
		AppendPascalString(line, "\pRendering... ");
		AppendNumber(line, elapsedSeconds);
		AppendPascalString(line, "\p sec elapsed");
	} else {
		AppendPascalString(line, "\pRender time: ");
		AppendNumber(line, elapsedSeconds);
		AppendPascalString(line, "\p sec");
	}
}

/* BuildZoomLine()/BuildMaxIterationsLine()/BuildConstantLine()
   The maths behind the current fractal - see the FractalParameters
   comment in mwWindow.h for which of these apply to which fractal.
   Zoom applies to any type with a view (FractalTypeHasView(), mwWindow.h -
   Lyapunov included, even though it isn't escape-time); Max Iterations
   and the constant line are escape-time-specific (Julia's own constant
   only, so far). NumToString() only formats whole numbers, so the
   floating-point values go through sprintf() (this project already
   links its ANSI library) and AppendCString() instead. */
static void BuildZoomLine(Str255 line, const FractalParameters *params) {
	char buffer[32];
	
	line[0] = 0;
	AppendPascalString(line, "\pZoom: ");
	sprintf(buffer, "%.4f", params->zoom);
	AppendCString(line, buffer);
}

static void BuildMaxIterationsLine(Str255 line, const FractalParameters *params) {
	line[0] = 0;
	AppendPascalString(line, "\pMax iterations: ");
	AppendNumber(line, params->maxIterations);
}

static void BuildConstantLine(Str255 line, const FractalParameters *params) {
	char buffer[32];
	
	line[0] = 0;
	AppendPascalString(line, "\pc = ");
	sprintf(buffer, "%.5f", params->constantRe);
	AppendCString(line, buffer);
	AppendPascalString(line, (params->constantIm >= 0.0) ? "\p + " : "\p - ");
	sprintf(buffer, "%.5f", fabs(params->constantIm));
	AppendCString(line, buffer);
	AppendPascalString(line, "\pi");
}

/* IsInfoWindow()/CloseInfoWindow()
   Let MandyWindow.c's event dispatch recognise and dismiss this
   window without needing gInfoWindow exposed directly - it stays
   private to this file, matching how mwWindow.c keeps mwWindow
   itself as the only thing anyone else needs a direct handle to. */
Boolean IsInfoWindow(WindowPtr w) {
	return (w != NULL) && (w == gInfoWindow);
}

void CloseInfoWindow(void) {
	if (gInfoWindow != NULL)
		HideWindow(gInfoWindow);
}

/* ShowInfoWindow()
   Creates the info window and its Close button the first time it's
   needed, or just brings an already-created one forward and up to
   date on later uses - matching how "Open" on the main fractal window
   already behaves (see mwMenus.c's fileID case). The button is
   created once and simply hidden along with the rest of the window
   thereafter (CloseInfoWindow() hides rather than disposes), so
   there's nothing further to set up on repeat uses. */
void ShowInfoWindow(void) {
	if (gInfoWindow == NULL) {
		Rect bounds, buttonRect;
		
		SetRect(&bounds, kInfoWindowLeft, kInfoWindowTop,
				kInfoWindowLeft + kInfoWindowWidth, kInfoWindowTop + kInfoWindowHeight);
		gInfoWindow = NewWindow(0L, &bounds, kInfoWindowTitle, true,
				kInfoWindowProcID, (WindowPtr) -1L, true, 0);
		
		SetRect(&buttonRect,
				(kInfoWindowWidth - kCloseButtonWidth) / 2,
				kInfoWindowHeight - kCloseButtonHeight - kCloseButtonBottomMargin,
				(kInfoWindowWidth - kCloseButtonWidth) / 2 + kCloseButtonWidth,
				kInfoWindowHeight - kCloseButtonBottomMargin);
		gCloseButton = NewControl(gInfoWindow, &buttonRect, kCloseButtonTitle,
				true, 0, 0, 0, pushButProc, 0L);
	} else {
		ShowWindow(gInfoWindow);
	}
	
	SelectWindow(gInfoWindow);
	gLastRefreshTick = TickCount();
}

/* DrawInfoWindowContent()
   Paints every line that applies to the current fractal, then the
   Close button on top. Called from MandyWindow.c's updateEvt case,
   between that BeginUpdate()/EndUpdate() pair, exactly like
   DrawContent() is for the main window.
   
   The 9pt font is set here (rather than once at window creation)
   because font/size are per-port state, and re-asserting it on every
   redraw costs nothing but guarantees it's never accidentally left in
   whatever a previous draw call set the port to. Line spacing comes
   from GetFontInfo() against that same 9pt setting rather than a
   guessed constant, so it stays correct if the font size here ever
   changes again. */
void DrawInfoWindowContent(void) {
	Str255				line;
	FontInfo			fontInfo;
	short				lineNumber = 0;
	short				lineHeight, firstBaseline;
	FractalParameters	params;
	
	if (gInfoWindow == NULL)
		return;
	
	SetPort(gInfoWindow);
	EraseRect(&gInfoWindow->portRect);
	
	TextFont(applFont);
	TextSize(kInfoFontSize);
	GetFontInfo(&fontInfo);
	lineHeight    = fontInfo.ascent + fontInfo.descent + fontInfo.leading;
	firstBaseline = kTopMargin + fontInfo.ascent;
	
	BuildFractalLine(line);
	DrawInfoLine(lineNumber++, line, firstBaseline, lineHeight);
	
	BuildResolutionLine(line);
	DrawInfoLine(lineNumber++, line, firstBaseline, lineHeight);
	
	BuildDepthLine(line);
	DrawInfoLine(lineNumber++, line, firstBaseline, lineHeight);
	
	BuildTimeLine(line);
	DrawInfoLine(lineNumber++, line, firstBaseline, lineHeight);
	
	params = GetFractalParameters();
	
	if (FractalTypeHasView(width)) {
		BuildZoomLine(line, &params);
		DrawInfoLine(lineNumber++, line, firstBaseline, lineHeight);
	}
	
	{
		FractalFamily family = FractalFamilyForWidth(width);
		if (family == kFractalFamilyEscapeTime || family == kFractalFamilyConvergence) {
			BuildMaxIterationsLine(line, &params);
			DrawInfoLine(lineNumber++, line, firstBaseline, lineHeight);
		}
	}
	
	if (FractalTypeHasFixedConstant(width)) {
		BuildConstantLine(line, &params);
		DrawInfoLine(lineNumber++, line, firstBaseline, lineHeight);
	}
	
	DrawControls(gInfoWindow);
}

/* HandleInfoWindowClick()
   Called from MandyWindow.c's HandleMouseDown() when a content click
   lands in this window (and it's already frontmost). where is in
   global coordinates, as EventRecord.where always is. */
void HandleInfoWindowClick(Point where) {
	ControlHandle	clickedControl;
	short			part;
	
	if (gInfoWindow == NULL)
		return;
	
	SetPort(gInfoWindow);
	GlobalToLocal(&where);
	
	part = FindControl(where, gInfoWindow, &clickedControl);
	
	if (part != 0 && clickedControl == gCloseButton) {
		if (TrackControl(clickedControl, where, NULL) != 0)
			CloseInfoWindow();
	}
}

/* RefreshInfoWindowIfNeeded()
   Called from HandleEvent()'s idle branch alongside
   AdvanceFractalRender(), so the elapsed-time line keeps counting up
   while a render is active. Refreshes roughly once a second while
   active, then exactly once more right when a render stops - to show
   its final time - and stays quiet after that, since nothing it
   displays changes again until a new render starts. Does nothing if
   the window doesn't exist or isn't currently visible. */
void RefreshInfoWindowIfNeeded(void) {
	unsigned long	now = TickCount();
	Boolean			isActive = IsRenderActive();
	Boolean			shouldRefresh;
	GrafPtr			savedPort;
	
	if (gInfoWindow == NULL || !((WindowPeek) gInfoWindow)->visible)
		return;
	
	shouldRefresh = isActive
		? ((now - gLastRefreshTick) >= kRefreshIntervalTicks)
		: gWasRenderActiveLastCheck;
	
	gWasRenderActiveLastCheck = isActive;
	
	if (!shouldRefresh)
		return;
	
	gLastRefreshTick = now;
	
	GetPort(&savedPort);
	SetPort(gInfoWindow);
	InvalRect(&gInfoWindow->portRect);
	SetPort(savedPort);
}
