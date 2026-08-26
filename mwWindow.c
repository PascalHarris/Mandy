/*****
 * mwWindow.c
 *
 *		The window routines for the Mandy Fractal Generator
 *
 *****/
#include <math.h>
#include <stdio.h>
#include "mwWindow.h"
#ifndef _Quickdraw_
#include <Quickdraw.h>
#endif

#define windowX 0
#define windowY 40
#define windowWidth 512
#define windowHeight 300
#define pi 3.14159265

WindowPtr	mwWindow;
Rect		dragRect;
Rect		windowBounds = { windowY, windowX, windowY+windowHeight, windowX+windowWidth };
Rect		imageStart = {0, 0, windowHeight, windowWidth};
int			width = 5; 

/* Offscreen pixel store --------------------------------------------
   RenderFractalOffscreen() draws the current fractal into this buffer
   whenever its parameters change; DrawContent() then just copies the
   already-finished pixels onto the screen. This is the classic
   (pre-Color QuickDraw) offscreen-bitmap technique - a plain BitMap
   with a manually allocated baseAddr/rowBytes, wrapped in an ordinary
   GrafPort - so it works unmodified back to the original 512K Mac.
   A GWorld-based equivalent for colour Macs is separate work. */
static GrafPort	offscreenPort;
static BitMap	offscreenBits;
static Rect		offscreenBounds;
static Boolean	offscreenReady = false;

static Boolean	AllocateOffscreenStore(void);
static void		DisposeOffscreenStore(void);
static void		DrawCurrentFractal(void);

/* SetUpWindow()
   Create the Minimum Window window, and open it. */
void SetUpWindow(void) {
    dragRect = screenBits.bounds;
    
    mwWindow = NewWindow(0L, &windowBounds, "\pFractal Window", true, noGrowDocProc, (WindowPtr) -1L, true, 0);
    SetPort(mwWindow);
    
    RenderFractalOffscreen();
}

void DrawBranch(float x1, float y1, float angle, float depth) {
	if (depth != 0) {
		float x2 = x1 + cos(angle*(pi/180.0))*depth*10;
		float y2 = y1 + sin(angle*(pi/180.0))*depth*10;
		
		MoveTo(x1,windowHeight-y1);
		Line(x2-x1,y1-y2);
		
		DrawBranch(x2,y2,angle-20,depth-1);
		DrawBranch(x2,y2,angle+20,depth-1);
	
	}

}

void Julia() {
    double cRe, cIm;                     //real and imaginary part of the constant c, determinate shape of the Julia Set
    double newRe, newIm, oldRe, oldIm;   //real and imaginary parts of new and old
    double zoom = 1, moveX = 0, moveY = 0; //you can change these to zoom and change position
    int sizex = windowWidth, sizey = windowHeight;
//    ColorRGB color; //the RGB color value for the pixel
    int maxIterations = 300; //after how much iterations the function should stop
	int x,y;
    //pick some values for the constant c, this determines the shape of the Julia Set
    cRe = -0.7;
    cIm = 0.27015;

    //loop through every pixel
    for(x = 0; x < sizex; x+=2)
    for(y = 0; y < sizey; y+=2)
    {
        //i will represent the number of iterations
    	int i;
        //calculate the initial real and imaginary part of z, based on the pixel location and zoom and position values
        newRe = 1.5 * (x - sizex / 2) / (0.5 * zoom * sizex) + moveX;
        newIm = (y - sizey / 2) / (0.5 * zoom * sizey) + moveY;
        //start the iteration process
        for(i = 0; i < maxIterations; i++)
        {
            //remember value of previous iteration
            oldRe = newRe;
            oldIm = newIm;
            //the actual iteration, the real and imaginary part are calculated
            newRe = oldRe * oldRe - oldIm * oldIm + cRe;
            newIm = 2 * oldRe * oldIm + cIm;
            //if the point is outside the circle with radius 2: stop
            if((newRe * newRe + newIm * newIm) > 4) break;
        }
        //draw the pixel
                 if (i>4) { //6 //12 //24
                MoveTo(x,y);
                if (i>32) {
                	Line (1,0);
                	MoveTo(x,y+1);
                	Line (1,0);
                } else if (i>24) {
	                Line (0,1);
	                Line (1,0);
                } else if (i>12) {
                	Line (1,0);
                } else if (i>6) {
                	Line (1,1);
                } else {
	                Line (0,0);
                }
            }

    }
}

void Mandelbrot() {
    //Mandelbrot
    int sizex = windowWidth, sizey = windowHeight;
    int maxiter = 64;
    int x = 0, y = 0;
    int zoom=150;

     for (x = 0; x < 2*(float)sizex; x+=2) {
        float xi = (float)x/zoom-2;
        for (y = 0; y < (float)sizey; y+=2) {
            float yi = (float)y / zoom;
            float px = 0;
            float py = 0;
            int i;
            for (i = 1; i < maxiter; i++) {
                float xt;
                if ( px*px+py*py > 4 ) {
                    break;
                }
                xt = xi + px*px-py*py;
                py = yi + 2*px*py;
                px = xt;
            }
            if (i>4) { //6 //12 //24
                MoveTo(x,(windowHeight/2)+y);
                if (i>32) {
                	Line (1,0);
                	MoveTo(x,(windowHeight/2)+y+1);
                	Line (1,0);
                } else if (i>24) {
	                Line (0,1);
	                Line (1,0);
                } else if (i>12) {
                	Line (1,0);
                } else if (i>6) {
                	Line (1,1);
                } else {
	                Line (0,0);
                }
                MoveTo(x,(windowHeight/2)-y);
                 if (i>32) {
                	Line (1,0);
                	MoveTo(x,(windowHeight/2)-y-1);
                	Line (1,0);
                } else if (i>24) {
	                Line (0,1);
	                Line (1,0);
                } else if (i>12) {
                	Line (1,0);
                } else if (i>6) {
                	Line (1,1);
                } else {
	                Line (0,0);
                }
            }
        }
    } 
}

/* DrawCurrentFractal()
   Runs whichever fractal "width" currently selects, drawing into
   whatever port is current. Kept separate from RenderFractalOffscreen()
   so DrawContent()'s low-memory fallback can reuse the same dispatch
   logic without duplicating it (DRY). */
static void DrawCurrentFractal(void) {
    if (width==1) {
    	DrawBranch(windowWidth/2,0,90,9);    
    } else if (width==2) {
    	Mandelbrot();
    } else if (width==3) {
        Julia();
    }
}

/* AllocateOffscreenStore()
   Manually allocates a plain (non-colour) BitMap the size of
   imageStart and wraps it in a GrafPort so QuickDraw can target it
   directly. Returns false if there isn't enough memory to allocate it
   (a real possibility on a 512K Mac); callers must be able to cope
   with that by drawing straight to the window instead. */
static Boolean AllocateOffscreenStore(void) {
    short	storeWidth  = imageStart.right  - imageStart.left;
    short	storeHeight = imageStart.bottom - imageStart.top;
    long	storeRowBytes = ((long) (storeWidth + 15) / 16) * 2;
    Ptr		storeBaseAddr = NewPtr(storeRowBytes * (long) storeHeight);
    
    if (storeBaseAddr == NULL)
        return false;
    
    offscreenBounds = imageStart;
    offscreenBits.baseAddr = storeBaseAddr;
    offscreenBits.rowBytes = storeRowBytes;
    offscreenBits.bounds   = offscreenBounds;
    
    OpenPort(&offscreenPort);
    SetPort(&offscreenPort);
    SetPortBits(&offscreenBits);
    offscreenPort.portRect = offscreenBounds;
    RectRgn(offscreenPort.visRgn, &offscreenBounds);
    ClipRect(&offscreenBounds);
    
    offscreenReady = true;
    return true;
}

/* DisposeOffscreenStore()
   Frees the offscreen buffer so it can be reallocated at a new size
   (see HandleWindowResized()). Safe to call when nothing is
   currently allocated. */
static void DisposeOffscreenStore(void) {
    if (!offscreenReady)
        return;
    
    ClosePort(&offscreenPort);
    DisposePtr(offscreenBits.baseAddr);
    offscreenBits.baseAddr = NULL;
    offscreenReady = false;
}

/* RenderFractalOffscreen()
   Recomputes the current fractal into the offscreen store. Call this
   only when the fractal's parameters actually change - the fractal
   type (HandleMenu()'s fractalID case), a window resize
   (HandleWindowResized()), or a future zoom - never from an ordinary
   update event.
   
   Port switching is centralised here rather than duplicated inside
   DrawBranch()/Mandelbrot()/Julia() themselves: those three stay
   agnostic to where their output lands (single responsibility), and
   the save-current-port/switch/restore dance isn't repeated three
   times (DRY).
   
   If the offscreen store isn't available and can't be allocated (low
   memory), this does nothing; DrawContent() then falls back to
   recomputing the fractal directly into the window on every update. */
void RenderFractalOffscreen(void) {
    GrafPtr	savedPort;
    
    GetPort(&savedPort);
    
    if (!offscreenReady && !AllocateOffscreenStore()) {
        SetPort(savedPort);
        return;
    }
    
    SetPort(&offscreenPort);
    EraseRect(&offscreenBounds);
    DrawCurrentFractal();
    
    SetPort(savedPort);
}

/* HandleWindowResized()
   Frees and reallocates the offscreen store at the window's new size,
   then re-renders into it. Nothing calls this yet - the window has no
   grow box today - but it's ready for the resizing/zoom work (#5) to
   call once imageStart reflects the new size. */
void HandleWindowResized(void) {
    DisposeOffscreenStore();
    RenderFractalOffscreen();
}

/* DrawContent()
   Paints the window's content. Ordinary update events reach this
   often - bringing the window forward, dragging another window
   across it, etc. - so it deliberately does no fractal maths: it
   just copies the already-rendered offscreen pixels onto the screen.
   CopyBits() is clipped by QuickDraw to the destination port's
   current visRgn/clipRgn, which BeginUpdate() has already narrowed to
   the update region, so no extra clipping needs to be worked out here.
   
   If there's no offscreen store (allocation failed under low memory),
   this falls back to the original behaviour of recomputing the
   fractal directly into the window on every update. */
void DrawContent(short active) {
    SetPort(mwWindow);
    
    if (offscreenReady) {
        CopyBits(&offscreenBits, &mwWindow->portBits, &offscreenBounds, &imageStart, srcCopy, NULL);
    } else {
        EraseRect(&imageStart);
        DrawCurrentFractal();
    }
}
