/*****
 * mwParameterDialog.c
 *
 *		See mwParameterDialog.h.
 *
 *		REQUIRES a DLOG/DITL resource pair - a resource, not something
 *		this file can create - following the same conventions mwZoom.c
 *		and mwMenus.c already document their own dialogs with:
 *
 *		  "Parameter Entry", ID 131 (kParameterDialogID). Suggested
 *		  size around 340x220 px - roomy enough for a two-line prompt
 *		  and three label/field rows with clear spacing; exact bounds
 *		  aren't load-bearing anywhere in this file. DITL 131, in
 *		  EXACTLY this item order - unlike mwZoom.c's confirmation
 *		  dialogs, most of these items have no title text of their own
 *		  to find them by at runtime, so item number is the only
 *		  handle this code has on them:
 *
 *		    1. StaticText, prompt - two or three lines tall, close to
 *		       the dialog's own width. Placeholder content only; every
 *		       call overwrites it via SetIText().
 *		    2. StaticText, field 1's label - placeholder content only.
 *		    3. EditText,   field 1's value.
 *		    4. StaticText, field 2's label - placeholder content only.
 *		    5. EditText,   field 2's value.
 *		    6. StaticText, field 3's label - placeholder content only.
 *		    7. EditText,   field 3's value.
 *		    8. Button,     titled exactly "OK".
 *		    9. Button,     titled exactly "Cancel".
 *
 *		  Item 1 needs to be tall enough for two or three lines of
 *		  text (SetIText() doesn't reflow a StaticText item's own
 *		  bounds to fit longer content - whatever's set has to already
 *		  fit the rect ResEdit gives it). Items 2-7 are laid out as
 *		  three label/field rows, evenly spaced - which rows are
 *		  actually visible for a given call depends on fieldCount (see
 *		  mwParameterDialog.h); unused trailing rows are hidden, not
 *		  removed, so leave real vertical gaps between rows 1, 2, and 3
 *		  rather than crowding them, since a 1-field call still uses
 *		  the same dialog height with rows 2 and 3 simply blank.
 *
 *		  Buttons 8/9 are found by their own title text
 *		  (ButtonItemWithTitle() below) exactly the way mwZoom.c's own
 *		  confirmation dialogs are - the lesson that item, once real
 *		  testing caught ResEdit's own item order not matching what a
 *		  comment like this one specified, applies here as much as it
 *		  did there.
 *
 *		  DLOG: references DITL 131, procID dBoxProc, goAway off,
 *		  "initially visible" doesn't matter - ShowParameterDialog()
 *		  forces it either way, matching every other dialog in this
 *		  project.
 *
 *****/
#include "mwParameterDialog.h"
#include "mwZoom.h"		/* CentreDialogOverMainWindow() */
#ifndef _Quickdraw_
#include <Quickdraw.h>
#endif
#ifndef _Dialogs_
#include <Dialogs.h>
#endif

#define kParameterDialogID		131

#define kPromptItem				1
#define kField1LabelItem		2
#define kField1EditItem			3
#define kFieldItemStride		2		/* field N's label/edit items are kField1LabelItem/kField1EditItem + (N-1)*kFieldItemStride */
#define kOKButtonTitle			"\pOK"
#define kCancelButtonTitle		"\pCancel"

/* CToPascalString()/PascalToCString()
   Plain length-prefixed <-> NUL-terminated conversion, capped at 255/
   63 characters respectively (Str255's own limit; ParameterField.text's).
   Small, and duplicated in spirit from mwInfo.c's AppendCString() and
   mwMenus.c's own BuildPascalString() rather than shared - each is a
   handful of lines, and this project already tolerates that
   (AppendCString() itself exists because Str255 has no equivalent
   built in) rather than introducing a shared string-utilities file for
   three call sites. Worth consolidating if a fourth one ever shows up. */
static void CToPascalString(const char *src, Str255 dst) {
	short i = 0;
	
	while (src[i] != '\0' && i < 255) {
		dst[i + 1] = src[i];
		i++;
	}
	dst[0] = i;
}

static void PascalToCString(const Str255 src, char *dst, short dstSize) {
	short i;
	short length = src[0];
	
	if (length > dstSize - 1)
		length = dstSize - 1;
	
	for (i = 0; i < length; i++)
		dst[i] = src[i + 1];
	dst[length] = '\0';
}

/* PascalStringsEqual()
   A plain byte-by-byte Pascal string comparison - duplicated from
   mwZoom.c's own helper of the same name and same reasoning: avoiding
   the Script Manager's EqualString() for a comparison this simple, so
   as not to add a dependency on a call this project has never actually
   linked before. */
static Boolean PascalStringsEqual(const unsigned char *a, const unsigned char *b) {
	short length = a[0];
	short i;
	
	if (length != b[0])
		return false;
	
	for (i = 1; i <= length; i++)
		if (a[i] != b[i])
			return false;
	
	return true;
}

/* The two items ButtonItemWithTitle() (below) searches - items 8 and
   9 as specified, but named for what real testing elsewhere in this
   project already warned not to trust blindly: a "guess" at where
   they'll actually be, confirmed by title text before being relied on
   for anything, never assumed correct on its own. */
#define kOKItemGuess		8
#define kCancelItemGuess	9

/* ButtonItemWithTitle()
   Which of this dialog's two buttons has the given title - see this
   file's own top comment for why title text, not item number, is what
   finds OK/Cancel specifically (items 2-7 have no title of their own
   to search for the same way, so they're still addressed by number).
   GetCTitle(), not GetIText(): a button is a Control Manager item, and
   mwZoom.c's own DialogItemTitleIs() already established GetCTitle()
   as this project's way of reading one's title back, rather than
   introducing a second, untested way to do the same thing here.
   Returns 0 if neither matches, which no caller here should ever
   actually see given the resource is meant to have exactly one button
   titled "OK" and one titled "Cancel". */
static short ButtonItemWithTitle(DialogPtr dialog, ConstStr255Param title) {
	short	item;
	
	for (item = kOKItemGuess; item <= kCancelItemGuess; item++) {
		short	itemType;
		Handle	itemHandle;
		Rect	itemRect;
		Str255	itemTitle;
		
		GetDItem(dialog, item, &itemType, &itemHandle, &itemRect);
		if ((itemType & 0x7F) != 4)		/* 4 = ctrlItem (a button), masking off the 128 = itemDisable bit - same test ShowAboutBox() (mwMenus.c) uses to find its own button */
			continue;
		
		GetCTitle((ControlHandle) itemHandle, itemTitle);
		if (PascalStringsEqual(itemTitle, title))
			return item;
	}
	
	return 0;
}

/* ShowParameterDialog()
   See mwParameterDialog.h. */
Boolean ShowParameterDialog(const char *prompt, ParameterField *fields, short fieldCount) {
	DialogPtr	dialog;
	Handle		dlogResource;
	GrafPtr		savedPort;
	short		itemType;
	Handle		itemHandle;
	Rect		itemRect;
	short		itemHit;
	short		okItem, cancelItem;
	short		i;
	Boolean		result;
	Str255		converted;
	
	GetPort(&savedPort);
	
	dlogResource = GetResource('DLOG', kParameterDialogID);
	if (dlogResource == NULL || ResError() != noErr) {
		SysBeep(1);
		return false;
	}
	
	dialog = GetNewDialog(kParameterDialogID, NULL, (WindowPtr) -1L);
	if (dialog == NULL) {
		SysBeep(1);
		return false;
	}
	
	HideWindow(dialog);
	SetPort(dialog);
	
	CToPascalString(prompt, converted);
	GetDItem(dialog, kPromptItem, &itemType, &itemHandle, &itemRect);
	SetIText(itemHandle, converted);
	
	for (i = 0; i < kMaxParameterFields; i++) {
		short labelItem = kField1LabelItem + i * kFieldItemStride;
		short fieldItem = kField1EditItem  + i * kFieldItemStride;
		
		if (i < fieldCount) {
			CToPascalString(fields[i].label, converted);
			GetDItem(dialog, labelItem, &itemType, &itemHandle, &itemRect);
			SetIText(itemHandle, converted);
			
			if (fields[i].kind == kParameterFieldInteger)
				NumToString(fields[i].value, converted);
			else
				CToPascalString(fields[i].text, converted);
			
			GetDItem(dialog, fieldItem, &itemType, &itemHandle, &itemRect);
			SetIText(itemHandle, converted);
		} else {
			HideDItem(dialog, labelItem);
			HideDItem(dialog, fieldItem);
		}
	}
	
	okItem     = ButtonItemWithTitle(dialog, (ConstStr255Param) kOKButtonTitle);
	cancelItem = ButtonItemWithTitle(dialog, (ConstStr255Param) kCancelButtonTitle);
	
	/* Falls back to the specified item numbers if title-matching found
	   neither - a misbuilt resource (wrong title text on a button)
	   would otherwise leave okItem/cancelItem both 0, and since
	   nothing a person can actually click ever produces itemHit==0,
	   the modal loop below would hang waiting for a click that can
	   never satisfy its own exit condition. Preferring the title match
	   when it succeeds, same as mwZoom.c's own dialogs, still protects
	   against ResEdit's item order not matching this file's own
	   numbered spec; this only guards the case where it doesn't match
	   *either* way. */
	if (okItem == 0)
		okItem = kOKItemGuess;
	if (cancelItem == 0)
		cancelItem = kCancelItemGuess;
	
	CentreDialogOverMainWindow(dialog);
	ShowWindow(dialog);
	SelectWindow(dialog);
	SelIText(dialog, kField1EditItem, 0, 32767);
	
	for (;;) {
		ModalDialog(NULL, &itemHit);
		
		if (itemHit == cancelItem) {
			result = false;
			break;
		}
		
		if (itemHit == okItem) {
			Boolean	allValid = true;
			short	firstBadField = -1;
			
			/* Validate every active field before committing any of
			   them, so a bad second field can't leave the first
			   field's result half-applied if this attempt turns out
			   invalid overall. */
			for (i = 0; i < fieldCount; i++) {
				short	fieldItem = kField1EditItem + i * kFieldItemStride;
				Str255	entered;
				
				GetDItem(dialog, fieldItem, &itemType, &itemHandle, &itemRect);
				GetIText(itemHandle, entered);
				
				if (fields[i].kind == kParameterFieldInteger) {
					long parsed;
					
					/* StringToNum() has no way to report "not a
					   number at all" - empty or non-numeric text
					   parses as 0, same as a genuine "0" would - so
					   the range check below is what actually catches
					   both a bad number and a missing one, not this
					   call itself. */
					StringToNum(entered, &parsed);
					
					if (parsed < fields[i].minimum || parsed > fields[i].maximum) {
						allValid = false;
						if (firstBadField < 0)
							firstBadField = i;
					} else {
						fields[i].value = parsed;
					}
				} else {
					if (entered[0] == 0) {
						allValid = false;
						if (firstBadField < 0)
							firstBadField = i;
					} else {
						PascalToCString(entered, fields[i].text, sizeof(fields[i].text));
					}
				}
			}
			
			if (allValid) {
				result = true;
				break;
			}
			
			SysBeep(1);
			SelIText(dialog, kField1EditItem + firstBadField * kFieldItemStride, 0, 32767);
		}
	}
	
	DisposeDialog(dialog);
	SetPort(savedPort);
	
	return result;
}
