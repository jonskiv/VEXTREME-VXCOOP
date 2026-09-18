/*
 * vxt_smart_text.c - see vxt_smart_text.h for the full design rationale.
 * Copyright (C) 2026 Caelotronics.
 *
 * DRIVER, NOT A FONT: this file has no glyph data of its own. It walks
 * whatever table vxtSmartTextSetFontTable() registered (vxt_font.h) and
 * turns each glyph's strokes into SmartList move/draw records. Link a
 * font module (e.g. vxt_font_western.c) and call that setter once at
 * startup before any text call.
 */
#include "vxt_smart_text.h"
#include "vxt_smart.h"

#define VXT_TEXT_HORIZ      0x12   /* IDENTICAL to vxt_text.c's values - see  */
#define VXT_TEXT_VERT       0x12   /* that file's header if these ever need  */
#define VXT_TEXT_SCALE      0x08   /* to change; kept in sync by hand, not a */
/* VXT_TEXT_POS_SCALE now lives in vxt_smart_text.h - callers must divide
 * their origins by it, so it is part of this module's contract. */
#define VXT_TEXT_GAP        1
#define VXT_TEXT_WIDTH      3

static const VxtFontGlyph *font_table;
static unsigned font_count;

void vxtSmartTextSetFontTable(const VxtFontGlyph *table, unsigned count)
{
    font_table = table;
    font_count = count;
}

static int8_t pen_x, pen_y;
static uint8_t text_intensity = 40;   /* VXT_INTENSITY_DIM's value - a
                                       * reasonable default; callers should
                                       * set explicitly via vxtSmartTextSetIntensity() */

/* Tracks which SmartList "run" is currently open, so consecutive strokes of
 * the SAME kind (move-then-move, or draw-then-draw) chain via SM_continue_d
 * (cheap - no shift-register touch) rather than each paying a fresh
 * SM_startMove_d/SM_startDraw_d. -1 = none open yet (forces a fresh start
 * on the very first stroke). */
#define MODE_NONE  -1
#define MODE_MOVE   0
#define MODE_DRAW   1
static int run_mode = MODE_NONE;

/* See vxtSmartTextSetScale()'s own header comment
 * (vxt_smart_text.h) for why this exists and the nop-padding safety note.
 * Replaces the fixed VXT_TEXT_SCALE constant everywhere it used to be used
 * directly for a stroke/advance move - defaults to that same value, so no
 * existing caller's output changes until one explicitly calls
 * vxtSmartTextSetScale(). */
static uint8_t text_scale = VXT_TEXT_SCALE;

void vxtSmartTextSetScale(uint8_t scale)
{
    text_scale = scale;
}

void vxtSmartTextSetIntensity(uint8_t intensity)
{
    text_intensity = intensity;
}

static const VxtFontGlyph *findGlyph(char c)
{
    unsigned i;
    for (i = 0; i < font_count; i++) {
        if (font_table[i].ch == c) return &font_table[i];
    }
    return 0;
}

/* See vxt_smart_text.h for the full rationale. Read-only accessor to the
 * registered font's glyph stroke table, for a caller that needs each
 * character's raw local-space strokes to transform/project as its own
 * geometry, rather than going through this module's own flat, fixed-scale
 * pen. */
uint8_t vxtSmartTextGlyphStrokes(char c, int8_t strokesOut[VXT_TEXT_MAX_STROKES][4])
{
    unsigned i;
    const VxtFontGlyph *g = findGlyph(c);

    if (!g) return 0;
    for (i = 0; i < g->nstrokes; i++) {
        strokesOut[i][0] = g->strokes[i].x1;
        strokesOut[i][1] = g->strokes[i].y1;
        strokesOut[i][2] = g->strokes[i].x2;
        strokesOut[i][3] = g->strokes[i].y2;
    }
    return g->nstrokes;
}

/* Rotation is applied to the emitted deltas only, at the single choke point
 * both smartMoveTo() and smartDrawTo() pass through - which is also how the
 * per-character advance gets rotated for free, since that advance is itself
 * a smartMoveTo() call (see vxtSmartTextChar()'s tail). Glyph coordinates
 * and pen_x/pen_y stay in unrotated glyph space, so a font never needs a
 * second, pre-rotated copy of itself.
 *
 * VXT_TEXT_ORIENT_HORIZ takes the early return, so that orientation emits
 * the same records as an unrotated caller. */
static uint8_t text_orient = VXT_TEXT_ORIENT_HORIZ;

void vxtSmartTextSetOrientation(uint8_t orient)
{
    text_orient = (orient > VXT_TEXT_ORIENT_180) ? VXT_TEXT_ORIENT_HORIZ : orient;
}

static void applyOrient(int8_t *dy, int8_t *dx)
{
    int8_t y = *dy, x = *dx;

    switch (text_orient) {
    case VXT_TEXT_ORIENT_CW:   *dy = (int8_t)-x; *dx = y;            break;
    case VXT_TEXT_ORIENT_CCW:  *dy = x;          *dx = (int8_t)-y;   break;
    case VXT_TEXT_ORIENT_180:  *dy = (int8_t)-y; *dx = (int8_t)-x;   break;
    default:                                                         break;
    }
}

static void smartMoveTo(int8_t x, int8_t y)
{
    int8_t dy, dx;
    if (x == pen_x && y == pen_y) return;   /* same skip as vxt_text's moveTo */
    dy = (int8_t)((y - pen_y) * VXT_TEXT_VERT);
    dx = (int8_t)((x - pen_x) * VXT_TEXT_HORIZ);
    applyOrient(&dy, &dx);
    if (run_mode == MODE_MOVE) vxtSmartCont(dy, dx);
    else { vxtSmartMove(dy, dx); run_mode = MODE_MOVE; }
    pen_x = x; pen_y = y;
}

static void smartDrawTo(int8_t x, int8_t y)
{
    int8_t dy, dx;
    dy = (int8_t)((y - pen_y) * VXT_TEXT_VERT);
    dx = (int8_t)((x - pen_x) * VXT_TEXT_HORIZ);
    applyOrient(&dy, &dx);
    if (run_mode == MODE_DRAW) vxtSmartCont(dy, dx);
    else {
        /* A fresh draw run needs the CURRENT text_intensity applied. Unlike
         * vxt_draw's format (intensity travels WITH every record), SmartList
         * separates intensity into its own SM_setIntensity record - so it is
         * (re-)emitted here, once per draw-run-start, change-gated inside
         * vxtSmartIntensity() itself (a no-op if already the active value -
         * see vxt_smart.c). This mirrors the box-grid pattern exactly. */
        vxtSmartIntensity(text_intensity);
        vxtSmartDraw(dy, dx);
        run_mode = MODE_DRAW;
    }
    pen_x = x; pen_y = y;
}

/* PER-CHARACTER SKEW COMPENSATION. Closes a real gap:
 * this module draws through its OWN smartMoveTo()/smartDrawTo() chain,
 * entirely independent of gamelib_beam.c - so `gamelibBeamSetDrawGain()`
 * (a game's first applied calibration correction) never touched text at
 * all, even though the calibration rig's TEXT H/TEXT V screens measured
 * text as the LARGEST error found so far: a 24-character string ends up
 * roughly 1,800 units low and 950 right of its ideal end point - about
 * three character heights.
 *
 * String STARTS measured clean (the vxtSmartTextBegin() reposition is fine);
 * the error accumulates PER CHARACTER as the string draws, at a fairly
 * consistent ~-80 Y / +40 X per glyph (calib.2's 16- and 24-char rows agree
 * to within a few percent). That per-CHARACTER-count behavior, not
 * per-drawn-DISTANCE, is why this needs its own mechanism rather than
 * reusing gamelib_beam.c's gain (which is fitted as % of distance and
 * confirmed roughly constant over an 8x length range on plain lines - a
 * different, and differently-caused, error).
 *
 * Applied ONCE PER CHARACTER, folded directly into the existing once-per-
 * glyph advance move (smartAdvanceChar(), below) - so this costs ZERO extra
 * records: the correction changes the numbers inside a
 * record that was already being emitted, exactly the same technique as the
 * gamelib_beam.c gain fix.
 *
 * Expressed in LOCAL (pre-rotation) space - "cross" is the axis
 * PERPENDICULAR to reading direction, "along" is parallel to it - and
 * applied BEFORE applyOrient() rotates the delta. That means one constant
 * pair is meant to serve every orientation (_HORIZ/_CW/_CCW/_180) via the
 * same rotation the module already does, rather than needing a separate
 * fitted pair per orientation.
 *
 * HONEST CAVEAT: the (cross, along) pair below was fitted ONLY from TEXT H
 * (horizontal) data. TEXT V's measured magnitudes (~33% along-axis error
 * vs ~7% cross-axis, over a much longer effective run since it advances at
 * VXT_TEXT_VERT not _HORIZ) do not obviously confirm this is a clean
 * rotation-invariant constant - that would need TEXT V's own numbers
 * checked against what rotating this pair 90 degrees predicts, which has
 * not been done. Treat this as a first-order correction for horizontal
 * text, unconfirmed (not contradicted, just unconfirmed) for rotated text.
 * Sticky like vxtSmartTextSetOrientation() - not reset per string. */
static int8_t text_comp_cross = 0;
static int8_t text_comp_along = 0;

void vxtSmartTextSetSkewComp(int8_t crossComp, int8_t alongComp)
{
    text_comp_cross = crossComp;
    text_comp_along = alongComp;
}

/* The once-per-glyph advance, local (0,0) -> (WIDTH+GAP, 0) - i.e. dy=0,
 * dx=(WIDTH+GAP)*HORIZ before compensation - with text_comp_cross/along
 * folded directly into that same delta, in local space, before applyOrient()
 * rotates it. Deliberately NOT a generic smartMoveTo() call: this is the
 * ONE specific move the compensation targets (see the comment above), not
 * every move this module makes - the intra-glyph stroke moves and the
 * "return to (0,0)" reset stay uncompensated, since only the ONE cumulative
 * per-character step is what the calibration rig actually measured. */
static void smartAdvanceChar(void)
{
    int8_t x = (int8_t)(VXT_TEXT_WIDTH + VXT_TEXT_GAP);
    int8_t dy, dx;

    if (x == pen_x && 0 == pen_y) return;
    dy = (int8_t)((0 - pen_y) * VXT_TEXT_VERT + text_comp_cross);
    dx = (int8_t)((x - pen_x) * VXT_TEXT_HORIZ + text_comp_along);
    applyOrient(&dy, &dx);
    if (run_mode == MODE_MOVE) vxtSmartCont(dy, dx);
    else { vxtSmartMove(dy, dx); run_mode = MODE_MOVE; }
    pen_x = x; pen_y = 0;
}

void vxtSmartTextBegin(int16_t originY, int16_t originX)
{
    /* CONFIRMED HARDWARE FIX (found via an independent game diagnostic):
     * vxtSmartRecenter() called immediately
     * after an OPEN (unclosed) draw run leaks a brief unintended visible
     * stroke. This module's own run_mode still reflects its last stroke
     * (this variable is NOT reset per-frame, only at the end of THIS
     * function) - close it before recentering if so. Applies directly to
     * Test 6c, which calls vxtSmartTextBegin() 4 times per frame
     * (LINES=/CH1=/CH2=/CH3=), each one following the previous text's own
     * open draw. */
    if (run_mode == MODE_DRAW) {
        vxtSmartMove(0, 0);
    }

    /* Recenter + position, at POS_SCALE - reuses SM_startMoveBig_d via
     * vxtSmartMoveBig, the SAME scale-32-safe routine already built and
     * verified for the box grid's row repositioning (see vxt_smart.h). */
    vxtSmartRecenter();
    vxtSmartScale(VXT_TEXT_POS_SCALE);
    vxtSmartMoveBig(originY * (int32_t)VXT_TEXT_POS_SCALE,
                    originX * (int32_t)VXT_TEXT_POS_SCALE,
                    VXT_TEXT_POS_SCALE);
    vxtSmartScale(text_scale);   /* strokes run at this scale from here on */
    pen_x = 0; pen_y = 0;
    run_mode = MODE_NONE;   /* force a fresh start/draw for the first stroke */
}

void vxtSmartTextChar(char c)
{
    unsigned i;
    const VxtFontGlyph *g = findGlyph(c);

    if (g) {
        for (i = 0; i < g->nstrokes; i++) {
            smartMoveTo(g->strokes[i].x1, g->strokes[i].y1);
            smartDrawTo(g->strokes[i].x2, g->strokes[i].y2);
        }
    }
    smartMoveTo(0, 0);
    smartAdvanceChar();   /* folds the per-character skew compensation
                          * into this same record, see smartAdvanceChar()'s
                          * own comment. */
    pen_x = 0; pen_y = 0;
}

void vxtSmartTextStr(const char *s)
{
    while (*s) {
        vxtSmartTextChar(*s++);
    }
}

void vxtSmartTextNumber(int16_t value)
{
    char buf[5];
    int i = 0;
    uint16_t mag;
    int neg = (value < 0);

    mag = (uint16_t)(neg ? -value : value);
    if (mag > 999) mag = 999;

    if (mag >= 100) buf[i++] = (char)('0' + mag / 100), mag = (uint16_t)(mag % 100);
    if (i || mag >= 10) buf[i++] = (char)('0' + mag / 10), mag = (uint16_t)(mag % 10);
    buf[i++] = (char)('0' + mag);
    buf[i] = '\0';

    if (neg) vxtSmartTextChar('-');
    vxtSmartTextStr(buf);
}

/* See this function's own header comment in
 * vxt_smart_text.h for why it's separate from vxtSmartTextNumber() rather
 * than widening that one. Same digit-printing shape, just int32_t/5
 * digits instead of int16_t/3. */
void vxtSmartTextNumber32(int32_t value)
{
    char buf[6];
    int i = 0;
    uint32_t mag;
    int neg = (value < 0);

    mag = (uint32_t)(neg ? -value : value);
    if (mag > 99999UL) mag = 99999UL;

    if (mag >= 10000UL) buf[i++] = (char)('0' + mag / 10000UL), mag = mag % 10000UL;
    if (i || mag >= 1000UL) buf[i++] = (char)('0' + mag / 1000UL), mag = mag % 1000UL;
    if (i || mag >= 100UL) buf[i++] = (char)('0' + mag / 100UL), mag = mag % 100UL;
    if (i || mag >= 10UL) buf[i++] = (char)('0' + mag / 10UL), mag = mag % 10UL;
    buf[i++] = (char)('0' + mag);
    buf[i] = '\0';

    if (neg) vxtSmartTextChar('-');
    vxtSmartTextStr(buf);
}

int32_t vxtSmartTextWidthPhys(int nchars)
{
    /* Real bug found from hardware feedback ("HUD text is
     * skewed wrong"): this formula computed the NOMINAL per-character
     * advance, with no knowledge that smartAdvanceChar() (added earlier
     * for skew compensation) can emit a DIFFERENT actual advance per
     * character. Every caller that centers or right-aligns text
     * (a menu title/options, HUD readouts, showcase titles) was
     * therefore computing its origin from a width that no longer matched
     * what actually got drawn - the same assumed-vs-achieved mismatch this
     * project root-caused once already elsewhere, this time
     * self-inflicted by the skew-comp addition. Different callers use this
     * differently (HUD right-aligns, menu/showcase center), which is why
     * the two symptoms looked different on hardware despite one root cause.
     *
     * Now reflects the ACTUAL per-character advance including whatever skew
     * compensation is currently set - text_comp_along is 0 by default
     * (identity), so this is byte-identical to the old formula until a
     * caller sets a nonzero compensation. */
    return (int32_t)nchars * ((VXT_TEXT_WIDTH + VXT_TEXT_GAP) * VXT_TEXT_HORIZ
                              + text_comp_along) * text_scale;
}
