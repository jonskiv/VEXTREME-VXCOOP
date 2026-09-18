/*
 * vxt_smart_text.h - VXT toolkit: text rendering via the SmartList engine
 * Copyright (C) 2026 Caelotronics.
 * (vxt_smart), not vxt_draw. GPLv3.
 *
 * vxt_text.c draws through vxtFrameVector, the vxt_draw wire format, which
 * costs approximately 90 cycles per record against vxt_smart's approximately
 * 39 (SM_continue_d). Text is disproportionately expensive on the slower
 * engine: a handful of short strings can consume over a third of the frame
 * budget. This module renders the same text on the SmartList engine
 * instead, as a parallel implementation; vxt_text.c is untouched and
 * remains available for existing callers.
 *
 * Driver, not a font. This module owns no glyph data. A font is a plain
 * data module built against the vxt_font.h contract (one array of
 * VxtFontGlyph entries); register one with vxtSmartTextSetFontTable()
 * once at startup, before any text call. See vxt_font.h for the format,
 * and vxt_font_western.c for a worked example. Swapping fonts, or writing
 * a new one, never touches this file.
 *
 * Two simplifications apply here that would not apply to arbitrary
 * SmartList geometry:
 *   1. Text uses one scale (VXT_TEXT_SCALE) for every stroke, moves and
 *      draws alike, so there is only one scale in play for the strokes and
 *      no risk of a stale scale left from a different caller.
 *   2. VXT_TEXT_SCALE (8) is under Malban's approximately 9-10 threshold
 *      where extra NOP padding becomes necessary at all, so the existing
 *      1-NOP-padded SM_startMove_d/SM_startDraw_d/SM_continue_d routines
 *      (tuned for scale 12, also under the threshold) already carry more
 *      padding than scale 8 needs; extra padding is always safe,
 *      only a shortfall causes drift. No new 6809 routine is needed for
 *      strokes. The one place a large jump is needed, the initial position
 *      to the string's screen location, reuses SM_startMoveBig_d, the same
 *      scale-32-safe routine used for other repositioning.
 *
 * Composition model: this module does not call vxtSmartBegin()/
 * vxtSmartEnd() itself. It is designed to be composed into a larger list
 * alongside other SmartList content, the caller bracketing the whole
 * frame's list with one vxtSmartBegin()/vxtSmartEnd() pair. Calling these
 * functions outside an active vxtSmartBegin()...vxtSmartEnd() span does
 * nothing, the same fails-blank-not-wild behavior vxt_smart itself has
 * when the address handshake isn't ready.
 */
#ifndef VXT_SMART_TEXT_H
#define VXT_SMART_TEXT_H

#include <stdint.h>
#include "vxt_font.h"

/* Selects which font table vxtSmartTextChar()/vxtSmartTextStr()/
 * vxtSmartTextGlyphStrokes() draw from. Must be called once at startup,
 * before any of those - this module carries no glyph data of its own; it
 * is a driver over whatever table is registered here (see vxt_font.h).
 * `table` must stay valid for the life of the program - pass a static
 * const array, normally a font module's own exported table. */
void vxtSmartTextSetFontTable(const VxtFontGlyph *table, unsigned count);

/* TEXT ORIENTATION, for a Vectrex physically rotated onto its side (the
 * console tilted 90 degrees, so text must be drawn rotated to read
 * correctly to the player). Requested from real experience shipping a
 * rotated-text routine for a real game; the calibration rig
 * (vxt/vxt_cal.c) measures both orientations because a long string's drift
 * is not assumed to behave the same along X as along Y.
 *
 * Implemented as a rotation of the EMITTED rate deltas only - glyph data,
 * pen bookkeeping and vxtSmartTextWidthPhys() are all unchanged and stay in
 * unrotated glyph space. HORIZ (0) is the default and emits byte-identical
 * records to before this existed, so no existing caller changes behavior.
 *
 * Sticky: set once, applies to every subsequent character until changed.
 * Not reset by vxtSmartTextBegin() - a caller drawing a rotated HUD sets it
 * once per frame, not once per string. */
#define VXT_TEXT_ORIENT_HORIZ   0   /* advance +X (default, unchanged)      */
#define VXT_TEXT_ORIENT_CW      1   /* rotated 90 clockwise, advance -Y     */
#define VXT_TEXT_ORIENT_CCW     2   /* rotated 90 anticlockwise, advance +Y */
#define VXT_TEXT_ORIENT_180     3   /* upside down, advance -X              */
void vxtSmartTextSetOrientation(uint8_t orient);

/* Per-character skew compensation, closing the gap that
 * this module's own draw chain never went through gamelib_beam.c's draw-gain
 * fix. See vxt_smart_text.c's own comment (above smart_comp_cross/along) for
 * the measurement behind this, why it is per-CHARACTER not per-distance, and
 * an honest caveat about what is and isn't confirmed. Values are int8_t RATE
 * units (not phys units - divide a measured phys-per-character drift by
 * VXT_TEXT_SCALE to get the rate value), applied in LOCAL pre-rotation space
 * so one pair serves every vxtSmartTextSetOrientation() setting. Default 0,0
 * = inert; sticky, not reset per string. */
void vxtSmartTextSetSkewComp(int8_t crossComp, int8_t alongComp);

/* Read-only accessor to this module's own glyph stroke table, for a
 * caller that needs each character's raw local-space strokes to
 * transform/project as 3D geometry itself, not vxt_smart_text's own flat
 * fixed-scale 2D pen (a scrolling crawl-text effect, for instance). Copies
 * up to VXT_TEXT_MAX_STROKES strokes' (x1,y1,x2,y2) into strokesOut (each
 * coordinate in the same 0-2 (x) / 0-4 (y) local glyph grid
 * vxtSmartTextChar() itself draws from) and returns the count; unknown
 * characters return 0, same "silently blank" behavior vxtSmartTextChar()
 * has. Does not touch the pen/run-mode state - this is a pure table
 * lookup, safe to call from outside any vxtSmartBegin()/vxtSmartEnd()
 * span. */
#define VXT_TEXT_MAX_STROKES  7
uint8_t vxtSmartTextGlyphStrokes(char c, int8_t strokesOut[VXT_TEXT_MAX_STROKES][4]);

/* Overrides the stroke/advance scale every
 * subsequent vxtSmartTextBegin() uses, in place of the fixed VXT_TEXT_SCALE
 * (8) every caller got before this existed. Sticky, like
 * vxtSmartTextSetOrientation()/SetSkewComp() - NOT reset automatically, so
 * a caller that shrinks it must restore VXT_TEXT_SCALE_DEFAULT before
 * returning, or every text draw after it (menu, HUD, etc., same shared
 * module) stays shrunk. Only ever go SMALLER than the default, never
 * larger - see this module's own header note on why VXT_TEXT_SCALE (8) was
 * chosen (under Malban's ~9-10 nop-padding threshold): the existing
 * 1-nop-padded SM_startMove_d/SM_startDraw_d/SM_continue_d routines are
 * tuned with MORE padding than scale 8 needs, and "extra padding
 * is always safe, only a SHORTFALL causes drift" - so any scale <= 8 stays
 * safely inside that same padding budget, but a scale > 8 has not been
 * checked against it. */
#define VXT_TEXT_SCALE_DEFAULT  0x08
void vxtSmartTextSetScale(uint8_t scale);

void vxtSmartTextSetIntensity(uint8_t intensity);   /* full 0-127, no bit stolen */
/* THE ORIGIN UNITS, and the one thing a first-time caller gets wrong.
 *
 * vxtSmartTextBegin() positions the pen through the scale-32 reposition routine
 * and multiplies both arguments by VXT_TEXT_POS_SCALE internally. Its arguments
 * are therefore in REPOSITION-GRID UNITS, not physical units:
 *
 *     vxtSmartTextBegin(physY / VXT_TEXT_POS_SCALE,
 *                       physX / VXT_TEXT_POS_SCALE);
 *
 * Passing a physical coordinate places the text 32 times too far out - a nominal
 * y of 17000 lands at 544,000 against a screen half-height of 18000, so nothing
 * appears anywhere. Divide first.
 *
 * The division truncates, so an origin that is not a whole number of grid steps
 * is rounded down by up to 31 physical units. That is immaterial for placing a
 * line of text, but it means a nudge smaller than one grid step has no effect at
 * all - make deliberate adjustments multiples of VXT_TEXT_POS_SCALE. */
#define VXT_TEXT_POS_SCALE  0x20   /* 32 - the reposition grid */

void vxtSmartTextBegin(int16_t originY, int16_t originX);
void vxtSmartTextChar(char c);
void vxtSmartTextStr(const char *s);
void vxtSmartTextNumber(int16_t value);
/* For a score HUD ("$ #####", up to 5 digits) - plain
 * vxtSmartTextNumber() caps its displayed magnitude at 999 by design (see
 * its own comment) and every one of its existing callers is fine with
 * that; widening it would be an unrelated behavior change for those. This
 * is a SEPARATE function instead, same digit-printing shape, just an
 * int32_t input and a 5-digit cap (99999) instead of 3. */
void vxtSmartTextNumber32(int32_t value);
int32_t vxtSmartTextWidthPhys(int nchars);   /* same formula as vxt_text's,
                                              * kept identical so centering
                                              * math doesn't need to change
                                              * at call sites that switch */

#endif /* VXT_SMART_TEXT_H */
