/*****
 * mwSaveAs.c
 *
 *	Save As... - exports the current fractal image as a PICT file.
 *	Reads whatever GetOffscreenImage() (mwWindow.c) currently holds,
 *	complete, partial, or aborted - Save As is greyed out by
 *	mwMenus.c's AdjustMenus() while a render is actively in progress
 *	(see IsRenderActive()), so by the time this can run, nothing else
 *	is still writing to that buffer.
 *
 *****/
#include "mwSaveAs.h"
#include "mwWindow.h"
#ifndef _Quickdraw_
#include <Quickdraw.h>
#endif

extern	WindowPtr	mwWindow;

/* Swap in 45RPM Software's own registered creator code here once
   available; '????' is a placeholder so this doesn't silently claim
   a real application's identity in the meantime. The file type
   'PICT' is what actually matters for the Finder and other apps to
   recognise this as a picture, regardless of the creator code. */
#define kFileCreator	'????'
#define kFileType		'PICT'

/* Classic PICT files store 512 bytes of (conventionally zero-filled)
   padding before the actual picture data - historically reserved for
   the Print Manager's use, now just an expected convention that
   other applications' PICT readers rely on. */
#define kPictFileHeaderSize	512

static PicHandle	RecordPicture(const BitMap *sourceBits, const Rect *sourceBounds);
static Boolean		WritePictureToFile(PicHandle picture, ConstStr255Param fileName, short vRefNum);

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
static Boolean WritePictureToFile(PicHandle picture, ConstStr255Param fileName, short vRefNum) {
	OSErr	error;
	short	refNum;
	long	byteCount;
	Ptr		zeroHeader;
	
	error = Create(fileName, vRefNum, kFileCreator, kFileType);
	if (error != noErr && error != dupFNErr)
		return false;
	
	error = FSOpen(fileName, vRefNum, &refNum);
	if (error != noErr)
		return false;
	
	SetEOF(refNum, 0);	/* in case an existing file being overwritten was larger */
	
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
