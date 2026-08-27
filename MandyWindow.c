/*****
 * MandyWindow.c
 *
 *	A simple fractal generator
 *
 *
 *****/

#include "mwMenus.h"
#include "mwWindow.h"
#include "mwInfo.h"
#include "mwColorCycle.h"
#include <GestaltEqu.h>

extern	WindowPtr	mwWindow;
extern	Rect		dragRect;

Boolean	gHasColorQD;

void InitMacintosh(void);
void HandleMouseDown (EventRecord	*theEvent);
void HandleEvent(void);
static Boolean HasColorQuickDraw(void);

/* InitMacintosh()
   Initialize all the managers & memory */
void InitMacintosh(void) {
    MaxApplZone();
    
    InitGraf(&thePort);
    InitFonts();
    FlushEvents(everyEvent, 0);
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0L);
    InitCursor();
    
    gHasColorQD = HasColorQuickDraw();
}

/* HasColorQuickDraw()
   This gates not just RGBForeColor()/PaintRect()/NewCWindow() (which
   only need basic Color QuickDraw, gestalt8BitQD) but also NewGWorld()
   and friends, which are a distinct, later capability - "32-Bit
   QuickDraw" - that a real machine can lack even with basic colour
   present (an early colour Mac on a System without that extension,
   for instance). Checking gestalt32BitQD covers both, since it
   implies gestalt8BitQD. Calling a GWorld routine on a system that
   only has basic Color QuickDraw is exactly the kind of thing that
   can crash instead of failing gracefully, since the call may not
   exist as a real trap at all rather than returning an error.
   
   The SysEnvirons() fallback is for systems old enough to predate the
   Gestalt Manager - in practice that's early System 6 on 68000 Macs,
   which never had colour hardware anyway, so this branch is mostly
   defensive completeness. It can only report basic colour presence,
   not 32-Bit QuickDraw specifically, so it's a narrower guarantee
   than the Gestalt check above - accepted here since a real machine
   old enough to lack Gestalt but with a GWorld-capable colour card is
   vanishingly unlikely to exist. */
static Boolean HasColorQuickDraw(void) {
    long qdVersion;
    
    if (Gestalt(gestaltQuickdrawVersion, &qdVersion) == noErr)
        return qdVersion >= gestalt32BitQD;
    
    {
        SysEnvRec environment;
        
        if (SysEnvirons(1, &environment) == noErr)
            return environment.hasColorQD;
    }
    
    return false;
}

void HandleMouseDown (EventRecord *theEvent) {
    WindowPtr	theWindow;
    int			windowCode = FindWindow (theEvent->where, &theWindow);
    
    switch (windowCode) {
        case inSysWindow:
            SystemClick (theEvent, theWindow);
            break;
            
        case inMenuBar:
            AdjustMenus();
            HandleMenu(MenuSelect(theEvent->where));
            break;
            
        case inDrag:
            if (theWindow == mwWindow)
                DragWindow(mwWindow, theEvent->where, &dragRect);
            else if (IsInfoWindow(theWindow))
                DragWindow(theWindow, theEvent->where, &dragRect);
            break;
            
        case inContent:
            if (theWindow == mwWindow) {
                if (theWindow != FrontWindow())
                    SelectWindow(mwWindow);
                else
                    InvalRect(&mwWindow->portRect);
            } else if (IsInfoWindow(theWindow)) {
                if (theWindow != FrontWindow())
                    SelectWindow(theWindow);
                else
                    HandleInfoWindowClick(theEvent->where);
            }
            break;
            
        case inGoAway:
            if (theWindow == mwWindow &&
                TrackGoAway(mwWindow, theEvent->where))
                HideWindow(mwWindow);
            else if (IsInfoWindow(theWindow) &&
                TrackGoAway(theWindow, theEvent->where))
                CloseInfoWindow();
            break;
    }
}

void HandleEvent(void) {
    int	ok;
    EventRecord	theEvent;
    
    HiliteMenu(0);
    SystemTask ();		/* Handle desk accessories */
    
    ok = GetNextEvent (everyEvent, &theEvent);
    if (ok) {
        switch (theEvent.what) {
            case mouseDown:
                HandleMouseDown(&theEvent);
                break;
                
            case keyDown:
            case autoKey:
                if ((theEvent.modifiers & cmdKey) != 0) {
                    char keyChar = (char) (theEvent.message & charCodeMask);
                    
                    if (keyChar == '.') {
                        AbortFractalRender();
                    } else {
                        AdjustMenus();
                        HandleMenu(MenuKey(keyChar));
                    }
                }
                break;
                
            case updateEvt: {
                WindowPtr windowToUpdate = (WindowPtr) theEvent.message;
                
                if (windowToUpdate == mwWindow) {
                    BeginUpdate(mwWindow);
                    DrawContent(((WindowPeek) mwWindow)->hilited);
                    EndUpdate(mwWindow);
                } else if (IsInfoWindow(windowToUpdate)) {
                    BeginUpdate(windowToUpdate);
                    DrawInfoWindowContent();
                    EndUpdate(windowToUpdate);
                }
                break;
            }
                
            case activateEvt:
                InvalRect(&mwWindow->portRect);
                break;
        }
    } else {
        AdvanceFractalRender();
        RefreshInfoWindowIfNeeded();
        AnimationTask();
    }
}

void main(void) {
    InitMacintosh();
    SetUpMenus();
    SetUpWindow();
    
    for (;;) {
        HandleEvent();
    }
}
