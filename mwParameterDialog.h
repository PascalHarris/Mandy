/*****
 * mwParameterDialog.h
 *
 *		Public interface for mwParameterDialog.c - a single, reusable
 *		"enter one or more values" modal dialog, for any fractal type
 *		that needs a runtime-configurable parameter: Multibrot's power
 *		today, Lyapunov's driving sequence and whatever Newton settles
 *		on tomorrow. One dialog, one resource, shared by all of them,
 *		rather than a separate hand-built dialog per fractal.
 *
 *		REQUIRES a DLOG/DITL resource pair - see mwParameterDialog.c's
 *		own top-of-file comment for the exact layout, following the
 *		same convention mwZoom.c and mwMenus.c already document their
 *		own dialogs' resource requirements with.
 *
 *****/
#ifndef _mwParameterDialog_
#define _mwParameterDialog_

/* One field's request going in, and its result coming back.
   
   kParameterFieldInteger - a whole number, constrained to
   [minimum, maximum] inclusive. Anything outside that range, or text
   that isn't a number at all, re-prompts rather than being accepted -
   see ShowParameterDialog()'s own comment.
   
   kParameterFieldText - up to 63 characters of plain text, validated
   here only for "not empty". Deeper, fractal-specific validation
   (Lyapunov's driving sequence being letters A/B only, say) is the
   caller's own job, checked after ShowParameterDialog() returns true -
   this dialog has no way to know what makes a valid string for any
   particular fractal, and isn't the place to teach it one. A caller
   with its own stricter rule can just call ShowParameterDialog() again
   in a loop of its own when its check fails, with an updated prompt
   explaining why.
   
   label is shown beside the field (e.g. "Power (n):") - not copied
   further than the one call, so a string literal is fine. value/text
   are both in/out: set the initial/default value before calling, read
   back whatever was entered and confirmed valid afterwards - untouched
   if the person cancels instead. */
typedef enum {
	kParameterFieldInteger,
	kParameterFieldText
} ParameterFieldKind;

typedef struct {
	ParameterFieldKind	kind;
	const char			*label;
	long				value;			/* kParameterFieldInteger: in/out */
	long				minimum;		/* kParameterFieldInteger only */
	long				maximum;		/* kParameterFieldInteger only */
	char				text[64];		/* kParameterFieldText: in/out */
} ParameterField;

/* The dialog's resource has room for this many fields - see
   mwParameterDialog.c's own layout comment. Rows beyond fieldCount are
   hidden, not deleted, so the dialog's overall size and the OK/Cancel
   buttons' position stay fixed regardless of how many fields a given
   call actually uses - a plainer, safer choice than repositioning
   items at runtime for a purely cosmetic gain (some unused space below
   a 1- or 2-field call) this project has no way to verify visually
   without a compiler. */
#define kMaxParameterFields	3

/* Shows prompt (a line or two of instructions, e.g. "Choose the power
   for z^n + c.") above up to kMaxParameterFields fields, then OK and
   Cancel. Returns true if OK was chosen, with every field's value/text
   holding its validated result; false for Cancel, with every field
   left exactly as passed in. Invalid input re-prompts in place (beep,
   select the offending field's text so typing immediately replaces
   it, loop back into the same modal dialog) rather than dismissing -
   see the .c file. fieldCount must be between 1 and
   kMaxParameterFields inclusive. */
Boolean ShowParameterDialog(const char *prompt, ParameterField *fields, short fieldCount);

#endif	/* _mwParameterDialog_ */
