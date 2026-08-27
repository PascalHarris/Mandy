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
    AppendMenu(fractalMenu, "\pTree/T;Mandelbrot/M;Julia/J");
}

/* AdjustMenus()
   Enable or disable the items in the Edit menu if a DA window
   comes up or goes away. Our application doesn't do anything with 
   the Edit menu.
   
   Save As is disabled while a render is actively in progress, since
   the offscreen store it would read is still being written to -
   see IsRenderActive() in mwWindow.c. A finished OR aborted render
   leaves it enabled either way, since GetOffscreenImage() (which
   Save As reads from) doesn't distinguish those two - whatever's in
   the buffer is fair game to save once nothing is actively changing
   it. */
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
             CheckItem(fractalMenu, width, false);
             width = menuItem;
             RenderFractalOffscreen();
             InvalRect(&mwWindow->portRect);
             break;
    }
}
