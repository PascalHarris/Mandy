/*****
 * mwMenus.c
 *
 *	Routines for Mandy Fractal Generator menus.
 *
 *****/

#include "mwMenus.h"
#include "mwWindow.h"
#include "mwInfo.h"
#include "mwSaveAs.h"
#include "mwColorCycle.h"

extern	WindowPtr mwWindow;
extern	int	width;


MenuHandle	appleMenu, fileMenu, editMenu, fractalMenu;

enum {
    appleID = 1,
    fileID,
    editID,
    fractalID
};

enum {
    openItem = 1,
    closeItem,
    getInfoItem = 4,
    saveAsItem,
    quitItem = 7
};

/* Fractal menu: Tree/Mandelbrot/Julia occupy items 1-3 (their item
   number is width - see HandleMenu()'s fractalID case); item 4 is a
   divider; animateItem is the "Animate"/"Stop Animation" toggle below
   it - see mwColorCycle.h; zoomOutItem, directly below that, resets
   gView to the current fractal's own default view - see
   ResetViewForCurrentFractal()/RestoreDefaultViewFromCache() in
   mwWindow.h. */
#define animateItem	5
#define zoomOutItem	6


/* SetUpMenus()
   Set up the menus. Normally, we’d use a resource file, but
   for this example we’ll supply “hardwired” strings. */
void SetUpMenus(void) {
    InsertMenu(appleMenu = NewMenu(appleID, "\p\024"), 0);
    InsertMenu(fileMenu = NewMenu(fileID, "\pFile"), 0);
    InsertMenu(editMenu = NewMenu(editID, "\pEdit"), 0);
   	InsertMenu(fractalMenu = NewMenu(fractalID, "\pFractal"), 0);
    DrawMenuBar();
    AddResMenu(appleMenu, 'DRVR');
    AppendMenu(fileMenu, "\pOpen/O;Close/W;(-;Get Info/I;Save As...;(-;Quit/Q");
    AppendMenu(editMenu, "\pUndo/Z;(-;Cut/X;Copy/C;Paste/V;Clear");
    AppendMenu(fractalMenu, "\pTree/T;Mandelbrot/M;Julia/J;(-;Animate;Zoom Out");
}

/* AdjustMenus()
   Enable or disable the items in the Edit menu if a DA window
   comes up or goes away. Our application doesn't do anything with 
   the Edit menu.
   
   Save As, Animate, and Zoom Out are all disabled while a render is
   actively in progress - Save As and Zoom Out because the offscreen
   store they'd read from or write to is still being written to by
   the render itself; Animate because AnimationTask() (see
   mwColorCycle.c) already declines to do anything mid-render anyway,
   so disabling the item just makes that visible rather than letting
   it look like a click did nothing. All three use IsRenderActive() in
   mwWindow.c. A finished OR aborted render leaves them enabled either
   way - Save As and Zoom Out because GetOffscreenImage() doesn't
   distinguish those two, and Animate because there's a real, if
   partial, image to animate regardless of how the render ended.
   
   Animate is also disabled outright when IsAnimationAvailable() says
   it wouldn't do anything useful even once a render finishes - the
   Tree fractal (no graduated shading to animate) or, in monochrome, a
   shade-level buffer that failed to allocate under low memory - rather
   than leaving the person to conclude animation is silently broken.
   IsAnimationActive() is included in that condition too, though, so
   switching to the Tree while animation is already running from a
   previous fractal never disables the item out from under a running
   "Stop Animation" - it stays clickable to turn off regardless of
   whether turning it on right now would be available.
   
   Zoom Out is similarly disabled outright for the Tree, via
   IsZoomOutAvailable() - it doesn't use gView at all, so there's
   nothing for the item to reset. */
static void enable (MenuHandle menu, short item, short ok);

void AdjustMenus(void) {
    register WindowPeek wp = (WindowPeek) FrontWindow();
    short kind = wp ? wp->windowKind : 0;
    Boolean DA = kind < 0;
    
    enable(editMenu, 1, DA);
    enable(editMenu, 3, DA);
    enable(editMenu, 4, DA);
    enable(editMenu, 5, DA);
    enable(editMenu, 6, DA);
    
    enable(fileMenu, openItem, !((WindowPeek) mwWindow)->visible);
    enable(fileMenu, closeItem, DA || ((WindowPeek) mwWindow)->visible);
    enable(fileMenu, saveAsItem, !IsRenderActive());
    
    enable(fractalMenu, animateItem, !IsRenderActive() && (IsAnimationAvailable() || IsAnimationActive()));
    enable(fractalMenu, zoomOutItem, !IsRenderActive() && IsZoomOutAvailable());
    
    //	CheckItem(widthMenu, width, true);
}

static
void enable(MenuHandle menu, short item, short ok) {
    if (ok)
        EnableItem(menu, item);
    else
        DisableItem(menu, item);
}

/* HandleMenu(mSelect)
   Handle the menu selection. mSelect is what MenuSelect() and
   MenuKey() return: the high word is the menu ID, the low word
   is the menu item */
void HandleMenu (long mSelect) {
    int			menuID = HiWord(mSelect);
    int			menuItem = LoWord(mSelect);
    Str255		name;
    GrafPtr		savePort;
    WindowPeek	frontWindow;
    
    switch (menuID) {
        case appleID:
            GetPort(&savePort);
            GetItem(appleMenu, menuItem, name);
            OpenDeskAcc(name);
            SetPort(savePort);
            break;
            
        case fileID:
            switch (menuItem) {
            case openItem:
                ShowWindow(mwWindow);
                SelectWindow(mwWindow);
                break;
                
            case closeItem:
                if ((frontWindow = (WindowPeek) FrontWindow()) == 0L)
                    break;
                
                if (frontWindow->windowKind < 0)
                    CloseDeskAcc(frontWindow->windowKind);
                else if ((frontWindow = (WindowPeek) mwWindow) != NULL)
                    HideWindow(mwWindow);
                break;
                
            case getInfoItem:
                ShowInfoWindow();
                break;
                
            case saveAsItem:
                SaveFractalAsPICT();
                break;
                
            case quitItem:
                ExitToShell();
                break;
        }
            break;
            
        case editID:
            if (!SystemEdit(menuItem-1))
                SysBeep(5);
            break;
            
     	case fractalID:
             if (menuItem == animateItem) {
                 ToggleAnimation();
                 SetItem(fractalMenu, animateItem, IsAnimationActive() ? "\pStop Animation" : "\pAnimate");
             } else if (menuItem == zoomOutItem) {
                 ResetViewForCurrentFractal();
                 if (!RestoreDefaultViewFromCache())
                     RenderFractalOffscreen();
                 InvalRect(&mwWindow->portRect);
             } else {
                 EnsureWindowVisible();
                 CheckItem(fractalMenu, width, false);
                 width = menuItem;
                 ResetViewForCurrentFractal();
                 RenderFractalOffscreen();
                 InvalRect(&mwWindow->portRect);
             }
             break;
    }
}
