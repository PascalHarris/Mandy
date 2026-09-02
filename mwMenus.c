/*****
 * mwMenus.c
 *
 *	Routines for Mandy Fractal Generator menus.
 *
 *	REQUIRES two more resources beyond what mwZoom.c already
 *	documents (the Zoom Confirmation/Insufficient Memory DLOG/DITL
 *	pairs at 128/129) - both for the About box:
 *
 *	  A DLOG/DITL pair at ID 130 ("About Mandy"). As actually built:
 *
 *	    DITL 130, three items in this order:
 *	      1. A Picture item, sized to exactly 120x60 px. ResEdit can
 *	         set this item's bounds but can't embed a specific PICT
 *	         into it directly - ShowAboutBox() attaches the actual
 *	         picture (GetPicture()/SetDItem()) at runtime, from the
 *	         PICT resource below.
 *	      2. A StaticText item reading "\xa9 2015 - 2026 45RPMSoftware"
 *	         (\xa9 is the copyright symbol, Option-G on a Mac keyboard)
 *	         - ShowAboutBox() sets 9pt Geneva on it at runtime, since a
 *	         DITL's own StaticText item has no way to specify a font or
 *	         size itself.
 *	      3. A Button, "OK".
 *
 *	    Same DLOG conventions as the existing dialogs (see mwZoom.c's
 *	    own top-of-file comment): procID dBoxProc, goAway off,
 *	    "initially visible" doesn't matter since the code forces it
 *	    either way.
 *
 *	  A PICT resource at ID 130, exactly 120x60 px - the actual image
 *	  (logo, artwork, whatever) the About box's picture item shows.
 *	  This one has to be created by hand (drawn or imported into
 *	  ResEdit's PICT editor) - it's image content, not something
 *	  derivable from this codebase.
 *
 *****/

#include "mwMenus.h"
#include "mwWindow.h"
#include "mwInfo.h"
#include "mwSaveAs.h"
#include "mwColourCycle.h"
#include "mwZoom.h"
#ifndef _Memory_
#include <Memory.h>
#endif

extern	WindowPtr mwWindow;
extern	int	width;


MenuHandle	appleMenu, fileMenu, editMenu, fractalMenu, paletteMenu;

enum {
    appleID = 1,
    fileID,
    editID,
    fractalID
};

/* The Apple menu's own first item, above the divider that separates
   it from the desk accessory list AddResMenu() appends in SetUpMenus() -
   see the standard "About <AppName>..." convention. Any other item
   clicked in this menu (i.e. anything past the divider) is a desk
   accessory name, handled as HandleMenu()'s appleID case always has. */
#define kAboutItem	1

/* About box resources - see the requirement documented at the top of
   this file. */
#define kAboutBoxDialogID	130
#define kAboutBoxPictureID	130
#define kAboutPictureItem	1
#define kAboutTextItem		2
#define kAboutOKItem		3

static void				ShowAboutBox(void);

/* The Palette submenu is its own MenuHandle, with its own menu ID,
   installed into the submenu portion of the menu list (InsertMenu()
   with hierMenu, in SetUpMenus()) rather than the visible menu bar -
   it only ever appears when the person points at the Palette item in
   the Fractal menu. Its ID just needs to not collide with
   appleID/fileID/editID/fractalID above; 10 leaves comfortable room
   either way. */
#define paletteMenuID	10

/* File menu: New Fractal (StartNewFractal(), mwWindow.h) resets to a
   blank "nothing selected" state; Open now loads a saved fractal-data
   file (LoadFractalData(), mwSaveAs.h) rather than its original
   "re-show the window" meaning, which New Fractal effectively takes
   over (see StartNewFractal()'s own comment) since Open needed to
   mean something else once fractal-data files existed to load. Close
   is unchanged. Save As... is split into two plain items rather than
   one dialog with a format choice - see next-improvements.md's §3 for
   why. */
enum {
    newFractalItem = 1,
    openItem,
    closeItem,
    getInfoItem = 5,
    savePictItem,
    saveFrctItem,
    quitItem = 9
};

/* Fractal menu: Tree/Mandelbrot/Julia occupy items 1-3 (their item
   number is width - see HandleMenu()'s fractalID case); item 4 is a
   divider; paletteItem is the hierarchical Palette submenu; animateItem
   is the "Animate"/"Stop Animation" toggle below it - see
   mwColourCycle.h; zoomOutItem, directly below that, resets gView to
   the current fractal's own default view - see
   ResetViewForCurrentFractal()/RestoreDefaultViewFromCache() in
   mwWindow.h. */
#define paletteItem	5
#define animateItem	6
#define zoomOutItem	7


/* SetUpMenus()
   Set up the menus. Normally, we’d use a resource file, but
   for this example we’ll supply “hardwired” strings. */
void SetUpMenus(void) {
    InsertMenu(appleMenu = NewMenu(appleID, "\p\024"), 0);
    InsertMenu(fileMenu = NewMenu(fileID, "\pFile"), 0);
    InsertMenu(editMenu = NewMenu(editID, "\pEdit"), 0);
   	InsertMenu(fractalMenu = NewMenu(fractalID, "\pFractal"), 0);
    DrawMenuBar();
    AppendMenu(appleMenu, "\pAbout Mandy 2;(-");
    AddResMenu(appleMenu, 'DRVR');
    AppendMenu(fileMenu, "\pNew Fractal/N;Open/O;Close/W;(-;Get Info/I;Save as PICT...;Save as Fractal Data...;(-;Quit/Q");
    AppendMenu(editMenu, "\pUndo/Z;(-;Cut/X;Copy/C;Paste/V;Clear");
    AppendMenu(fractalMenu, "\pTree/T;Mandelbrot/M;Julia/J;(-;Palette;Animate;Zoom Out");
    
    /* Palette submenu - see the comment above paletteMenuID. Item
       order in this AppendMenu() string must match kPalettes[] in
       mwWindow.c exactly, since HandleMenu()'s paletteMenuID case maps
       this menu's (1-based) item numbers straight onto that array's
       (0-based) indices. */
    paletteMenu = NewMenu(paletteMenuID, "\pPalette");
    AppendMenu(paletteMenu, "\pDefault;Night;Stormy;Summery;Autumnal;Wintery;Pastel;Rainbow;Fire;Ocean;Greyscale");
    InsertMenu(paletteMenu, -1);	/* -1 = hierMenu: install as a submenu, not into the visible menu bar */
    
    /* Locks this handle's block so it can never move. Real testing
       found the submenu stops opening at all - the "Palette" item in
       fractalMenu still shows its hierarchical arrow correctly, so
       that item's own cmd/mark fields (set below) are intact - the
       first time this happens is always right after a render
       actually finishes, never from idle time alone. EndRendering()
       (mwWindow.c) calls SetWTitle() at exactly that moment, which
       internally resizes the window's title storage - a genuine
       Memory Manager operation, capable of triggering heap
       compaction, that has nothing to do with this menu at all but
       runs at exactly the right moment to be a suspect. Locking
       paletteMenu here rules out (or fixes, if this really is the
       cause) that handle's block ever being the one that moves.
       Unconfirmed as the actual mechanism - a hierarchical submenu's
       entry in the Menu Manager's own internal list is documented
       (informally, by longtime Mac developers - see 68kMLA's "Weird
       Facts about the Menu Manager") as itself being a proper
       MenuHandle, which should already survive relocation - but
       locking a small menu record costs nothing, so it's worth
       trying regardless of that uncertainty. */
    HLock((Handle) paletteMenu);
    
    SetItemCmd(fractalMenu, paletteItem, 0x1B);	/* 0x1B = hMenuCmd: marks this item as a hierarchical menu's anchor */
    SetItemMark(fractalMenu, paletteItem, paletteMenuID);
    CheckItem(paletteMenu, GetCurrentPalette() + 1, true);
}

/* AdjustMenus()
   Enable or disable the items in the Edit menu if a DA window
   comes up or goes away. Our application doesn't do anything with 
   the Edit menu.
   
   New Fractal, Save as PICT..., Save as Fractal Data..., Animate, and
   Zoom Out are all disabled while a render is actively in progress -
   New Fractal because it disposes the offscreen store
   (StartNewFractal(), mwWindow.h), and Save As/Zoom Out because the
   offscreen store they'd read from or write to is still being written
   to by the render itself; Animate because AnimationTask() (see
   mwColourCycle.c) already declines to do anything mid-render anyway,
   so disabling the item just makes that visible rather than letting
   it look like a click did nothing. All of them use IsRenderActive()
   in mwWindow.c. A finished OR aborted render leaves them enabled
   either way - Save As and Zoom Out because GetOffscreenImage()
   doesn't distinguish those two, and Animate because there's a real,
   if partial, image to animate regardless of how the render ended.
   
   Save as PICT... and Save as Fractal Data... are also both disabled
   outright via HasRenderableImage() (mwWindow.h) - nothing to save,
   in either format, against a window that's never actually rendered
   anything (a fresh launch, before any fractal type has been picked,
   or New Fractal having just reset back to that same state).
   
   Open (now LoadFractalData() - see the enum comment above for why)
   and Get Info aren't gated on anything here: loading a fresh fractal
   is exactly as valid with nothing on screen yet as with something
   already there, the same way picking a fractal type from the Fractal
   menu always is, and Get Info was never gated before this either.
   
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
   IsZoomAvailable() - it doesn't use gView at all, so there's
   nothing for the item to reset.
   
   Palette follows the same "disabled during a render" convention for
   consistency with the rest of this menu, even though a palette
   change itself never touches the offscreen store's pixel data and
   so isn't actually unsafe mid-render (see SetCurrentPalette()'s own
   comment on this). It's also disabled outright via
   IsPaletteAvailable() on a black-and-white Mac, where there's no
   colour table for a palette to describe.
   
   Unlike the other three, this greying-out is done to the individual
   items *inside* the Palette submenu (SetPaletteMenuItemsEnabled()
   below), not to the "Palette" item itself in fractalMenu, which
   stays permanently enabled. Real testing found that disabling that
   parent item - the seemingly obvious approach, matching Animate/Zoom
   Out - left it looking enabled again once a render finished and
   AdjustMenus() re-enabled it, but its submenu stopped opening at all
   from that point on: the parent's own enabled bit was fine, but
   something about having been disabled and re-enabled broke its link
   to paletteMenu. What exactly the Menu Manager does internally that
   causes this wasn't pinned down - disabling a hierarchical item
   isn't something this project had done before, and it isn't
   something Inside Macintosh's own documentation says much about
   either way - so rather than guess further, the item disabling
   itself is avoided entirely: the parent is never touched, only the
   plain, ordinary items inside its submenu (exactly the kind of
   disabling Cut/Copy already gets in a normal Edit menu), which
   carries none of that risk. */
static void enable (MenuHandle menu, short item, short ok);
static void SetPaletteMenuItemsEnabled(Boolean enabled);

void AdjustMenus(void) {
    register WindowPeek wp = (WindowPeek) FrontWindow();
    short kind = wp ? wp->windowKind : 0;
    Boolean DA = kind < 0;
    
    enable(editMenu, 1, DA);
    enable(editMenu, 3, DA);
    enable(editMenu, 4, DA);
    enable(editMenu, 5, DA);
    enable(editMenu, 6, DA);
    
    enable(fileMenu, newFractalItem, !IsRenderActive());
    enable(fileMenu, closeItem, DA || ((WindowPeek) mwWindow)->visible);
    enable(fileMenu, savePictItem, !IsRenderActive() && HasRenderableImage());
    enable(fileMenu, saveFrctItem, !IsRenderActive() && HasRenderableImage());
    
    enable(fractalMenu, animateItem, !IsRenderActive() && (IsAnimationAvailable() || IsAnimationActive()));
    enable(fractalMenu, zoomOutItem, !IsRenderActive() && IsZoomAvailable());
    /* Re-asserts every part of the hierarchical link between
       fractalMenu's "Palette" item and paletteMenu, every single time
       - rather than trusting the one-time setup in SetUpMenus() to
       still hold. Real testing found the submenu simply stops opening
       after the first real render completes, for reasons that
       enabling/disabling the item (in either direction, on either
       menu) and locking paletteMenu's handle have all failed to
       explain or fix - see the long comment above. Since the actual
       mechanism hasn't been pinned down, this re-establishes the link
       from scratch on the working assumption that something is
       un-registering or disabling it rather than corrupting the
       parent item's own record (whose hierarchical arrow keeps
       showing correctly throughout, which is what pointed away from
       that). InsertMenu() is documented as doing nothing if the menu
       is already in the list, so calling it here on every single
       menu-bar click is free when nothing is actually wrong, and
       fixes it outright if paletteMenu has been silently dropped from
       the list, or from a disabled state, by whatever this is. */
    InsertMenu(paletteMenu, -1);
    EnableItem(paletteMenu, 0);
    SetItemCmd(fractalMenu, paletteItem, 0x1B);
    SetItemMark(fractalMenu, paletteItem, paletteMenuID);
    
    SetPaletteMenuItemsEnabled(!IsRenderActive() && IsPaletteAvailable());
    
    //	CheckItem(widthMenu, width, true);
}

static
void enable(MenuHandle menu, short item, short ok) {
    if (ok)
        EnableItem(menu, item);
    else
        DisableItem(menu, item);
}

/* SetPaletteMenuItemsEnabled()
   Enables or disables every item inside the Palette submenu - see the
   long comment above AdjustMenus() for why this targets the
   submenu's own items rather than the "Palette" item in fractalMenu
   that opens it. GetPaletteCount() (mwWindow.h) is exactly how many
   items AppendMenu() gave paletteMenu in SetUpMenus(), so this always
   covers all of them even if palettes are added or removed there in
   future. */
static void SetPaletteMenuItemsEnabled(Boolean enabled) {
    short count = GetPaletteCount();
    short i;
    
    for (i = 1; i <= count; i++)
        enable(paletteMenu, i, enabled);
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
            if (menuItem == kAboutItem) {
                ShowAboutBox();
            } else {
                GetPort(&savePort);
                GetItem(appleMenu, menuItem, name);
                OpenDeskAcc(name);
                SetPort(savePort);
            }
            break;
            
        case fileID:
            switch (menuItem) {
            case newFractalItem:
                StartNewFractal();
                EnsureWindowVisible();
                InvalRect(&mwWindow->portRect);
                break;
                
            case openItem:
                LoadFractalData();
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
                
            case savePictItem:
                SaveFractalAsPICT();
                break;
                
            case saveFrctItem:
                SaveFractalData();
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
             } else if (menuItem == paletteItem) {
                 /* The Palette item itself is just the hierarchical
                    menu's anchor (see SetItemCmd()/SetItemMark() in
                    SetUpMenus()) - it can't actually be chosen on its
                    own, so this case should never be reached in
                    practice, but costs nothing to have here rather
                    than silently falling through if it somehow is. */
             } else {
                 EnsureWindowVisible();
                 CheckItem(fractalMenu, width, false);
                 width = menuItem;
                 ResetViewForCurrentFractal();
                 RenderFractalOffscreen();
                 InvalRect(&mwWindow->portRect);
             }
             break;
             
        case paletteMenuID:
            CheckItem(paletteMenu, GetCurrentPalette() + 1, false);
            SetCurrentPalette(menuItem - 1);
            CheckItem(paletteMenu, menuItem, true);
            break;
    }
}

/* ShowAboutBox()
   "About Mandy 2" (HandleMenu()'s appleID case) - see the resource
   requirement documented at the top of this file (DLOG/DITL/PICT all
   at ID 130). Attaches the PICT to the picture item and sets 9pt
   Geneva on the text item at runtime, since neither is something a
   DITL resource can express on its own. Draws the standard
   thick-rounded-rect outline around the OK button by hand, the same
   way any dialog with a default button needs to (the Dialog Manager
   doesn't draw this itself for a plain button item) - visual only,
   answering to a mouse click on it exactly like any other button,
   not to Return/Enter. A real filter proc making the keyboard actually
   trigger it was tried and removed: ModalFilterProcPtr callbacks need
   Pascal calling convention, which Think C's `pascal` keyword only
   applies with Language Extensions enabled (Edit/Options/Language
   Settings) - real testing crashed on exactly this, consistent with
   that setting being off in this project and the filter proc's actual
   calling convention silently not matching what ModalDialog() expects
   when it calls back into it. Passing NULL instead sidesteps the
   question entirely, matching every other dialog in this project.
   
   Otherwise follows ShowConfirmationDialog()'s established pattern
   (mwZoom.c): the resource-existence check, hiding before
   CentreDialogOverMainWindow() repositions it, and forcing it visible
   regardless of the DLOG's own "initially visible" flag. A single-
   button dialog doesn't need that function's "find the button by its
   title text" logic, since there's only one button to wait for, so
   this is its own, simpler function rather than a third call to it. */
static void ShowAboutBox(void) {
	DialogPtr	dialog;
	Handle		dlogResource;
	PicHandle	picture;
	short		itemType;
	Handle		itemHandle;
	Rect		itemRect;
	Rect		buttonRect;
	short		buttonItem = 0;
	short		fontID;
	short		itemHit;
	GrafPtr		savedPort;
	short		i;
	
	GetPort(&savedPort);
	
	dlogResource = GetResource('DLOG', kAboutBoxDialogID);
	if (dlogResource == NULL || ResError() != noErr) {
		SysBeep(1);
		return;
	}
	
	dialog = GetNewDialog(kAboutBoxDialogID, NULL, (WindowPtr) -1L);
	if (dialog == NULL) {
		SysBeep(1);
		return;
	}
	
	HideWindow(dialog);
	
	picture = GetPicture(kAboutBoxPictureID);
	if (picture != NULL) {
		GetDItem(dialog, kAboutPictureItem, &itemType, &itemHandle, &itemRect);
		SetDItem(dialog, kAboutPictureItem, itemType, (Handle) picture, &itemRect);
	}
	
	SetPort(dialog);
	GetFNum("\pGeneva", &fontID);
	TextFont(fontID);
	TextSize(9);
	
	/* Finds the dialog's one button by its actual DITL item type (4 =
	   ctrlItem, masking off the 128 = itemDisable bit static text and
	   the picture item both carry) rather than assuming a fixed item
	   number - the Zoom Confirmation dialog earlier in this project
	   already found ResEdit's own item ordering doesn't reliably match
	   what's specified when a DITL is actually built, so this doesn't
	   number turns out to be (kAboutOKItem, above, documents the
	   layout as specified - item 3 - but the code below doesn't
	   actually trust it). Falls back to accepting the first click
	   on anything if no control item is found at all (shouldn't
	   happen, but a dialog with no way to dismiss it at all would be
	   worse than accepting an unexpected click). */
	for (i = 1; i <= 3; i++) {
		GetDItem(dialog, i, &itemType, &itemHandle, &itemRect);
		if ((itemType & 0x7F) == 4) {
			buttonItem = i;
			buttonRect = itemRect;
		}
	}
	
	CentreDialogOverMainWindow(dialog);
	
	ShowWindow(dialog);
	SelectWindow(dialog);
	
	if (buttonItem != 0) {
		InsetRect(&buttonRect, -4, -4);
		PenSize(3, 3);
		FrameRoundRect(&buttonRect, 16, 16);
		PenSize(1, 1);
	}
	
	do {
		ModalDialog(NULL, &itemHit);
	} while (buttonItem != 0 && itemHit != buttonItem);
	
	DisposeDialog(dialog);
	SetPort(savedPort);
}
