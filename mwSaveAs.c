/*****
 * mwSaveAs.c
 *
 *	Saving and loading fractal-related files: exporting the current
 *	image as a PICT (SaveFractalAsPICT()), saving the parameters
 *	needed to recompute it as a small text file (SaveFractalData()),
 *	and loading one of those text files back (LoadFractalData()).
 *	Reads whatever GetOffscreenImage() (mwWindow.c) currently holds,
 *	complete, partial, or aborted - Save As is greyed out by
 *	mwMenus.c's AdjustMenus() while a render is actively in progress
 *	(see IsRenderActive()), so by the time this can run, nothing else
 *	is still writing to that buffer.
 *
 *****/
#include "mwSaveAs.h"
#include "mwWindow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _Quickdraw_
#include <Quickdraw.h>
#endif

extern	WindowPtr	mwWindow;
extern	int			width;

/* 45RPM Software's registered creator code, used for every file this
   project creates (both PICT and FRCT - see kFileType/kFrctFileType
   below). Matching this exactly against the application's own
   creator code (set in Think C's Project Type settings, not in code -
   see the resource requirements this feature needs, documented
   separately) is what lets the Finder find this app's BNDL/FREF/ICN#
   resources and show the right icon for files it creates. */
#define kFileCreator	'MNDy'
#define kFileType		'PICT'
#define kFrctFileType	'FRCT'

/* Classic PICT files store 512 bytes of (conventionally zero-filled)
   padding before the actual picture data - historically reserved for
   the Print Manager's use, now just an expected convention that
   other applications' PICT readers rely on. */
#define kPictFileHeaderSize	512

/* A generous ceiling on a fractal-data file's size - the format is a
   handful of short "Key: value" lines (see SaveFractalData()'s own
   comment), so anything anywhere near this large is almost certainly
   the wrong kind of file, not a legitimately large one. Guards
   LoadFractalData()'s single whole-file NewPtr() against an
   unreasonable allocation rather than any real format need. */
#define kMaxFractalDataFileSize	4096

static PicHandle	RecordPicture(const BitMap *sourceBits, const Rect *sourceBounds);
static Boolean		CreateAndOpenForWriting(ConstStr255Param fileName, short vRefNum, OSType fileType, short *outRefNum);
static Boolean		WritePictureToFile(PicHandle picture, ConstStr255Param fileName, short vRefNum);
static Boolean		WriteTextToFile(const char *text, long length, ConstStr255Param fileName, short vRefNum);
static void			ApplyFractalDataText(char *text);

/* SaveFractalAsPICT()
   Prompts for a filename via the Standard File Package, records the
   current offscreen image as a PICT, and writes it out. Beeps and
   gives up at any failure point rather than raising an alert - this
   project has no alert/dialog resource of its own yet to raise one
   with, so a beep is the honest, minimal option rather than adding
   new UI infrastructure as a side effect of this feature. */
void SaveFractalAsPICT(void) {
	BitMap		*sourceBits;
	Rect		sourceBounds;
	SFReply		reply;
	Point		dialogLocation = { 100, 100 };
	PicHandle	picture;
	
	if (!GetOffscreenImage(&sourceBits, &sourceBounds)) {
		SysBeep(1);	/* nothing rendered yet to save */
		return;
	}
	
	SFPutFile(dialogLocation, "\pSave fractal as:", "\pFractal Picture", NULL, &reply);
	
	if (!reply.good)
		return;
	
	picture = RecordPicture(sourceBits, &sourceBounds);
	
	if (picture == NULL) {
		SysBeep(1);
		return;
	}
	
	if (!WritePictureToFile(picture, reply.fName, reply.vRefNum))
		SysBeep(1);
	
	KillPicture(picture);
}

/* RecordPicture()
   Wraps the offscreen image's existing pixel data as a PICT, via the
   standard OpenPicture()/ClosePicture() recording technique: any
   QuickDraw call made between the two is captured as picture opcodes
   instead of actually being drawn on screen.
   
   The CopyBits() destination is mwWindow's own portBits - the exact
   same call shape BlitOffscreenToWindow() already uses successfully
   every time the window redraws - rather than sourceBits used as both
   source and destination. That self-copy was the original approach
   here, on the reasoning that only the call's parameters matter during
   recording, not a real pixel copy; Apple's own Technical Note #405
   documents almost that exact pattern - CopyBits(myWindow^.portBits,
   myWindow^.portBits, ...) during recording - producing an empty
   result, with the destination pixel data ending up blank. Recording
   redirects the operation rather than performing it for real, so
   mwWindow's actual on-screen content shouldn't be affected either
   way; this just reuses a call already proven to transfer the right
   pixels, instead of the one shape Apple's own documentation shows
   misbehaving. */
static PicHandle RecordPicture(const BitMap *sourceBits, const Rect *sourceBounds) {
	PicHandle	picture;
	GrafPtr		savedPort;
	
	GetPort(&savedPort);
	SetPort(mwWindow);
	
	picture = OpenPicture(sourceBounds);
	if (picture != NULL)
		CopyBits(sourceBits, &mwWindow->portBits, sourceBounds, sourceBounds, srcCopy, NULL);
	ClosePicture();
	
	SetPort(savedPort);
	
	return picture;
}

/* WritePictureToFile()
   Creates (or truncates and reuses, if the chosen name already
   exists) the destination file, writes the 512-byte header PICT files
   conventionally start with, then the picture data itself. Returns
   false on any failure along the way. */
/* CreateAndOpenForWriting()
   Creates (or truncates and reuses, if the chosen name already
   exists) a file for writing, returning its open reference number via
   *outRefNum. Shared by WritePictureToFile() and WriteTextToFile() -
   both start with exactly this same Create()/FSOpen()/SetEOF()
   sequence, differing only in what they write afterward. Returns
   false (leaving *outRefNum untouched) on any failure. */
static Boolean CreateAndOpenForWriting(ConstStr255Param fileName, short vRefNum, OSType fileType, short *outRefNum) {
	OSErr	error;
	short	refNum;
	
	error = Create(fileName, vRefNum, kFileCreator, fileType);
	if (error != noErr && error != dupFNErr)
		return false;
	
	error = FSOpen(fileName, vRefNum, &refNum);
	if (error != noErr)
		return false;
	
	SetEOF(refNum, 0);	/* in case an existing file being overwritten was larger */
	
	*outRefNum = refNum;
	return true;
}

static Boolean WritePictureToFile(PicHandle picture, ConstStr255Param fileName, short vRefNum) {
	OSErr	error;
	short	refNum;
	long	byteCount;
	Ptr		zeroHeader;
	
	if (!CreateAndOpenForWriting(fileName, vRefNum, kFileType, &refNum))
		return false;
	
	zeroHeader = NewPtrClear((long) kPictFileHeaderSize);
	if (zeroHeader == NULL) {
		FSClose(refNum);
		return false;
	}
	
	byteCount = kPictFileHeaderSize;
	error = FSWrite(refNum, &byteCount, zeroHeader);
	DisposePtr(zeroHeader);
	
	if (error == noErr) {
		byteCount = GetHandleSize((Handle) picture);
		HLock((Handle) picture);
		error = FSWrite(refNum, &byteCount, *picture);
		HUnlock((Handle) picture);
	}
	
	FSClose(refNum);
	FlushVol(NULL, vRefNum);
	
	return (error == noErr);
}

/* WriteTextToFile()
   Writes length bytes of plain text to a file, via the same
   Create()/FSOpen()/SetEOF() sequence WritePictureToFile() uses (see
   CreateAndOpenForWriting()). Used by SaveFractalData() - text is
   written whole in one FSWrite() rather than line by line, since the
   whole file (a handful of short "Key: value" lines) is already
   sitting fully formatted in memory by the time this is called. */
static Boolean WriteTextToFile(const char *text, long length, ConstStr255Param fileName, short vRefNum) {
	OSErr	error;
	short	refNum;
	long	byteCount;
	
	if (!CreateAndOpenForWriting(fileName, vRefNum, kFrctFileType, &refNum))
		return false;
	
	byteCount = length;
	error = FSWrite(refNum, &byteCount, (Ptr) text);
	
	FSClose(refNum);
	FlushVol(NULL, vRefNum);
	
	return (error == noErr);
}


/* SaveFractalData()
   Prompts for a filename via the Standard File Package, and writes
   out a small text file recording enough to recompute the current
   fractal - never the image itself (that's what Save as PICT... is
   for) - as a series of "Key: value" lines, one per line, extensible
   by simply omitting keys that don't apply to the current fractal
   type and having LoadFractalData()/ApplyFractalDataText() ignore any
   key they don't recognise. Type is always present and is the
   fractal's own name (FractalTypeNameForWidth()) rather than width's
   raw numeric value, so a saved file's meaning survives even if new
   fractal types are ever inserted ahead of existing ones.
   CentreRe/CentreIm/HalfWidthRe (gView) are omitted for the Tree,
   which doesn't use gView at all; ConstantRe/ConstantIm
   (GetFractalParameters()) are present only for Julia, whose constant
   is currently fixed rather than user-adjustable, but are still
   written for forward-compatibility once/if that changes. Palette is
   always present (GetPaletteName()) - a palette is a display
   preference independent of the fractal itself, not something to omit
   based on fractal type.
   
   Lines end in \r, the classic Mac OS text-file convention, so the
   file reads correctly line-by-line in a plain text editor. Numbers
   are written to 6 decimal places - a reasonable middle ground given
   this project now has two, very different per-pixel precisions
   depending on gHasFPU (see FractalView's own comment in mwWindow.h):
   comfortably more than the fixed-point path's own effective
   precision at deep zoom, if noticeably less than the floating-point
   path's double could in principle make use of. gView itself is
   always double regardless of which path rendered it, so this is a
   choice about the saved file's own readability and portability
   between the two, not a limit imposed by either.
   
   Beeps and gives up at any failure point rather than raising an
   alert - see SaveFractalAsPICT()'s own comment on why. */
void SaveFractalData(void) {
	SFReply				reply;
	Point				dialogLocation = { 100, 100 };
	char				buffer[512];
	int					length = 0;
	FractalParameters	params;
	
	if (!HasRenderableImage()) {
		SysBeep(1);	/* nothing rendered yet to save */
		return;
	}
	
	length += sprintf(buffer + length, "Type: %s\r", FractalTypeNameForWidth(width));
	
	if (FractalTypeHasView(width)) {
		length += sprintf(buffer + length, "CentreRe: %.6f\r", gView.centreRe);
		length += sprintf(buffer + length, "CentreIm: %.6f\r", gView.centreIm);
		length += sprintf(buffer + length, "HalfWidthRe: %.6f\r", gView.halfWidthRe);
	}
	
	if (FractalTypeHasFixedConstant(width)) {
		params = GetFractalParameters();
		length += sprintf(buffer + length, "ConstantRe: %.6f\r", params.constantRe);
		length += sprintf(buffer + length, "ConstantIm: %.6f\r", params.constantIm);
	}
	
	/* Multibrot's and Newton's own power - identified by name, the same
	   way Type: itself is, rather than a hardcoded type ID: consistent
	   with why this whole file already avoids width's raw numeric value
	   wherever a fractal's identity actually matters. Both share the
	   same Power: key (only one can ever be the current type at once,
	   so there's no ambiguity in the file itself - see
	   ApplyFractalDataText()'s own gating on load). Not folded into
	   FractalTypeHasFixedConstant() above - that flag means "one fixed
	   value the registry itself already knows", where this is mutable,
	   user-chosen state living outside the registry entirely (see
	   GetMultibrotPower()'s/GetNewtonPower()'s own comments in
	   mwWindow.h). */
	if (strcmp(FractalTypeNameForWidth(width), "Multibrot") == 0)
		length += sprintf(buffer + length, "Power: %ld\r", GetMultibrotPower());
	else if (strcmp(FractalTypeNameForWidth(width), "Newton") == 0)
		length += sprintf(buffer + length, "Power: %ld\r", GetNewtonPower());
	
	length += sprintf(buffer + length, "Palette: %s\r", GetPaletteName(GetCurrentPalette()));
	
	SFPutFile(dialogLocation, "\pSave fractal data as:", "\pFractal Data", NULL, &reply);
	
	if (!reply.good)
		return;
	
	if (!WriteTextToFile(buffer, (long) length, reply.fName, reply.vRefNum))
		SysBeep(1);
}

/* LoadFractalData()
   Prompts for a file via the Standard File Package, filtered to the
   'FRCT' type SaveFractalData() writes (so the dialog only ever shows
   files this feature itself could have produced), reads it whole into
   memory, and hands the text to ApplyFractalDataText() to parse and
   apply. No gating on HasRenderableImage() or IsRenderActive() the way
   Save As/zooming/Animate have - loading a fresh fractal is exactly
   as valid with nothing on screen yet as with something already
   there, the same way choosing a fractal type from the Fractal menu
   always is. */
void LoadFractalData(void) {
	SFTypeList	typeList = { kFrctFileType, 0, 0, 0 };
	SFReply		reply;
	Point		dialogLocation = { 100, 100 };
	short		refNum;
	long		fileSize;
	OSErr		error;
	Ptr			buffer;
	
	SFGetFile(dialogLocation, "\p", NULL, 1, typeList, NULL, &reply);
	
	if (!reply.good)
		return;
	
	error = FSOpen(reply.fName, reply.vRefNum, &refNum);
	if (error != noErr) {
		SysBeep(1);
		return;
	}
	
	error = GetEOF(refNum, &fileSize);
	if (error != noErr || fileSize <= 0 || fileSize > kMaxFractalDataFileSize) {
		FSClose(refNum);
		SysBeep(1);
		return;
	}
	
	buffer = NewPtr(fileSize + 1);
	if (buffer == NULL) {
		FSClose(refNum);
		SysBeep(1);
		return;
	}
	
	error = FSRead(refNum, &fileSize, buffer);
	FSClose(refNum);
	
	if (error != noErr) {
		DisposePtr(buffer);
		SysBeep(1);
		return;
	}
	
	buffer[fileSize] = '\0';
	
	ApplyFractalDataText(buffer);
	
	DisposePtr(buffer);
}

/* ApplyFractalDataText()
   Parses text's "Key: value" lines (see SaveFractalData()'s own
   comment on the format) and applies whatever it finds: sets width
   from a recognised Type, resets to that fractal's own default view
   (ResetViewForCurrentFractal()) and then overwrites it with a saved
   CentreRe/CentreIm/HalfWidthRe if all were present, and switches to
   a recognised Palette. Unrecognised keys, and CentreRe/CentreIm/
   HalfWidthRe when they're absent (as they always will be for a saved
   Tree), are simply skipped rather than treated as errors - exactly
   the extensibility SaveFractalData()'s own comment describes.
   
   Modifies text in place (splitting it into NUL-terminated lines by
   overwriting each line ending) rather than copying - LoadFractalData()
   passes ownership of a buffer it's about to dispose right after this
   returns, so there's nothing to preserve. Line endings are read
   leniently (\r, \n, or \r\n) even though SaveFractalData() only ever
   writes \r, in case a file was ever edited elsewhere before being
   loaded back.
   
   If no recognised Type line is found at all, beeps and leaves
   everything - width, gView, the current palette - untouched, rather
   than rendering a blank or partially-updated fractal from a file
   that wasn't really in this format to begin with. */
static void ApplyFractalDataText(char *text) {
	char	*lineStart = text;
	short	newWidth = -1;
	double	centreRe = 0.0, centreIm = 0.0, halfWidthRe = 0.0;
	Boolean	haveView = false;
	short	paletteIndex = 0;
	Boolean	havePalette = false;
	long	power = 0;
	Boolean	havePower = false;
	
	while (*lineStart != '\0') {
		char	*lineEnd = lineStart;
		char	*nextLine;
		char	*colon;
		char	*value;
		
		while (*lineEnd != '\0' && *lineEnd != '\r' && *lineEnd != '\n')
			lineEnd++;
		
		nextLine = lineEnd;
		if (*nextLine == '\r' || *nextLine == '\n') {
			char terminator = *nextLine;
			nextLine++;
			if (terminator == '\r' && *nextLine == '\n')
				nextLine++;
		}
		*lineEnd = '\0';
		
		colon = strchr(lineStart, ':');
		if (colon != NULL) {
			*colon = '\0';
			value = colon + 1;
			while (*value == ' ')
				value++;
			
			if (strcmp(lineStart, "Type") == 0) {
				FindFractalTypeByName(value, &newWidth);
			} else if (strcmp(lineStart, "CentreRe") == 0) {
				centreRe = atof(value);
				haveView = true;
			} else if (strcmp(lineStart, "CentreIm") == 0) {
				centreIm = atof(value);
			} else if (strcmp(lineStart, "HalfWidthRe") == 0) {
				halfWidthRe = atof(value);
			} else if (strcmp(lineStart, "Palette") == 0) {
				havePalette = FindPaletteByName(value, &paletteIndex);
			} else if (strcmp(lineStart, "Power") == 0) {
				power = atol(value);
				havePower = true;
			}
		}
		
		lineStart = nextLine;
	}
	
	if (newWidth == -1) {
		SysBeep(1);	/* not a Type line this build recognises - nothing applied */
		return;
	}
	
	width = newWidth;
	ResetViewForCurrentFractal();
	
	if (haveView) {
		gView.centreRe    = centreRe;
		gView.centreIm    = centreIm;
		gView.halfWidthRe = ClampHalfWidthRe(halfWidthRe);
	}
	
	if (havePalette)
		SetCurrentPalette(paletteIndex);
	
	/* Power: is shared between Multibrot and Newton - both take a
	   configurable power and both save/load it under the same key (see
	   SaveFractalData() above), so applying it has to be gated by which
	   type was actually loaded, not applied unconditionally to one of
	   them the way it safely could be back when Multibrot was the only
	   type with a Power: line at all. */
	if (havePower) {
		const char *loadedName = FractalTypeNameForWidth(width);
		
		if (strcmp(loadedName, "Multibrot") == 0)
			SetMultibrotPower(power);
		else if (strcmp(loadedName, "Newton") == 0)
			SetNewtonPower(power);
	}
	
	EnsureWindowVisible();
	RenderFractalOffscreen();
	InvalRect(&mwWindow->portRect);
}
