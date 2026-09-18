/*
 * test6c_handler.c - TEST 6c: multi-channel sound (A/C/E chord, joystick
 * Copyright (C) 2026 Caelotronics.
 * select + tune) layered on the SmartList draw engine, box grid AND text
 * both drawn through vxt_smart/vxt_smart_text in ONE unified list.
 *
 * WHAT THIS DEMONSTRATES: a shared draw-and-sound RPC that reads discrete
 * (edge-triggered) input for a box-count control and a channel selector,
 * continuous (held) input for a frequency-tune control, drives all 3 AY
 * tone channels through one vxtSoundMixer() call, and draws a scalable
 * box grid plus a live readout - all from one region, one bracket.
 *
 * TEXT AND BOXES SHARE ONE vxtSmartBegin()/vxtSmartEnd() bracket: both are
 * the same wire format now, so the old box-only/text-only region split is
 * unnecessary and they can freely interleave.
 *
 * See vxt_input.h's own header for the discrete-vs-continuous input
 * pattern this handler follows for the two joystick controls.
 */

#include "vxt_frame.h"
#include "vxt_input.h"
#include "vxt_sound.h"
#include "vxt_smart.h"
#include "vxt_smart_text.h"

#define TEST6C_RPC_ID  71

/* ---- diagnostic toggle: 0 = normal, 1 = boxes only, 2 = text only ---- */
#define T6C_DIAG  0

/* ---- units: physical displacement = rate x duration ---- */
#define POS_SCALE       0x20            /* keep equal to VXT_TEXT_POS_SCALE */
#define PHYS(rate, dur) ((int32_t)(rate) * (dur))

/* Screen extent, calibrated against real hardware (the tube is portrait,
 * so the two axes genuinely differ - a single SCREEN_HALF is wrong). */
#define SCREEN_HALF_X  13500
#define SCREEN_HALF_Y  18000

#define TEXT_Y       (-SCREEN_HALF_Y + 2200)   /* readout at the true bottom */
#define GRID_TOP     (SCREEN_HALF_Y - 1200)    /* grid starts at the true top */
#define GRID_BOTTOM  (TEXT_Y + 3000)           /* clear of the readout        */

/* ---- box geometry: full size, no divisor - the screen has the room ---- */
#define BOX_EDGE_RATE   0x40
#define BOX_EDGE_DUR    0x0C
#define BOX_EDGE_PHYS   PHYS(BOX_EDGE_RATE, BOX_EDGE_DUR)   /* 768 */
#define BOX_GAP_PHYS    (BOX_EDGE_PHYS / 2)
#define BOX_PITCH_PHYS  (BOX_EDGE_PHYS + BOX_GAP_PHYS)

/* GRID_HALF_X is a DEDICATED, conservative constant for the box row
 * specifically - narrower than SCREEN_HALF_X, which stays untouched
 * because the CH1/CH2/CH3 and LINES= text positioning depend on it and
 * has no similar margin problem. A hardware photo showed a formula using
 * SCREEN_HALF_X directly let one too many boxes fit on paper while the
 * last one drifted partly off-screen in practice - GRID_HALF_X exists to
 * carry that measured margin instead of a theoretical one. */
#define GRID_HALF_X    12300
#define BOXES_PER_ROW  (((2 * GRID_HALF_X) - BOX_EDGE_PHYS) / BOX_PITCH_PHYS + 1)
#define MAX_ROWS       (((GRID_TOP - GRID_BOTTOM) - BOX_EDGE_PHYS) / BOX_PITCH_PHYS + 1)
#define GRID_LEFT_PHYS (-(((BOXES_PER_ROW - 1) * BOX_PITCH_PHYS + BOX_EDGE_PHYS) / 2))

/* Compile-time self-check: this cannot catch a miscalibrated CONSTANT
 * against real, unmeasured hardware, but it does guarantee that any
 * FUTURE change to box size, pitch, or GRID_HALF_X that would push a box
 * edge outside the assumed bound fails the BUILD, not a hardware photo
 * weeks later. Recomputes the same quantities with raw literals (no
 * PHYS() cast) since the preprocessor's #if cannot parse a C type cast. */
#define PP_BOX_EDGE       (BOX_EDGE_RATE * BOX_EDGE_DUR)
#define PP_BOX_PITCH      (PP_BOX_EDGE + PP_BOX_EDGE / 2)
#define PP_BOXES_PER_ROW  (((2 * GRID_HALF_X) - PP_BOX_EDGE) / PP_BOX_PITCH + 1)
#define PP_GRID_HALFSPAN  (((PP_BOXES_PER_ROW - 1) * PP_BOX_PITCH + PP_BOX_EDGE) / 2)

#if PP_GRID_HALFSPAN > GRID_HALF_X
#error "test6c grid: computed box row exceeds GRID_HALF_X - box would draw off-screen. Reduce BOXES_PER_ROW, box size, or increase GRID_HALF_X only after re-measuring on real hardware."
#endif

#define MIN_BOXES  1
#define FIT_BOXES  (BOXES_PER_ROW * MAX_ROWS)

#define TEST6C_TEXT_OFFSET  0x1C00

/* Record-budget cap: the compositor recenters+absolutely-repositions once
 * per row (not per box), so worst-case corners need a handful of chained
 * MoveBig records to reposition, before drawing 4 edges. T6C_TEXT_RESERVE
 * is an EMPIRICALLY MEASURED worst case for the 4 readout strings
 * ("LINES = 999","CH1=999","CH2=999","CH3=999"), not a guess - a prior
 * estimate that was too low, combined with a units bug at the
 * vxtSmartBegin() call site, caused overflow at 1 box; this gives real
 * margin above the measured value. */
#define T6C_TOTAL_RECORDS        ((0x2000 - VXT_SMART_OFFSET) / 4)
#define T6C_TEXT_RESERVE_RECORDS 240
#define T6C_RECORD_BUDGET  (((T6C_TOTAL_RECORDS - T6C_TEXT_RESERVE_RECORDS)) / 6)
#define T6C_MAX_BOXES      (FIT_BOXES < T6C_RECORD_BUDGET ? FIT_BOXES : T6C_RECORD_BUDGET)

static const uint8_t INTENSITY_LEVELS[4] = {
    VXT_INTENSITY_DIM, VXT_INTENSITY_MEDIUM, VXT_INTENSITY_BRIGHT, VXT_INTENSITY_MAX
};

/* --- box-count state (buttons 3/4) - held-mask pattern, a continuous
 * control (adjust a value while held) - see vxt_input.h's own header for
 * why this differs from the discrete channel-select pattern below. */
static uint8_t t6c_mask3 = 0, t6c_mask4 = 0;
static int t6c_one_bit(uint8_t v) { return v && !(v & (uint8_t)(v - 1)); }
static int t6c_box_count = 30;

/* --- channel state --- */
/* Initial chord: A4/C5/E5, computed from the AY clock at 1.5MHz
 * (period = clock/(16*freq)). Storing FREQUENCY (Hz), not period, since
 * that is what the display shows and what the joystick adjusts; period
 * is derived at sound-emit time. */
static int16_t ch_freq[3] = { 440, 523, 659 };   /* A4, C5, E5 */
static int     sel_channel = 0;                  /* 0,1,2 -> CH1,CH2,CH3 */

#define FREQ_MIN     80
#define FREQ_MAX     2000
#define FREQ_STEP    2      /* Hz/frame while joystick held up/down */

static uint16_t freqToPeriod(int16_t freq)
{
    /* period = 1,500,000 / (16 * freq), confirmed AY clock. Integer
     * arithmetic, rounded rather than truncated. */
    int32_t denom = 16L * freq;
    return (uint16_t)((1500000L + denom / 2) / denom);
}

/* --- joystick edge state for channel select (left/right = one step) --- */
static int8_t joyx_prev = 0;
static uint8_t joyx_neutral_frames = 0;   /* debounce - see channel-select below */

void test6c_handler(uint8_t id, volatile uint8_t *parm)
{
    uint8_t held, edge3, edge4;
    int btn3, btn4;
    int8_t joyx, joyy;
    int i;
    (void)id;

    /* ---- box count (buttons 3/4), continuous while held ---- */
    held  = parm[VXT_IN_BTNS];
    edge3 = parm[VXT_IN_BTN1_3];
    edge4 = parm[VXT_IN_BTN1_4];
    if (edge3 && !t6c_mask3 && t6c_one_bit(held)) t6c_mask3 = held;
    if (edge4 && !t6c_mask4 && t6c_one_bit(held)) t6c_mask4 = held;
    btn3 = t6c_mask3 ? ((held & t6c_mask3) != 0) : (edge3 != 0);
    btn4 = t6c_mask4 ? ((held & t6c_mask4) != 0) : (edge4 != 0);
    if (btn3 && t6c_box_count > MIN_BOXES) t6c_box_count--;
    if (btn4 && t6c_box_count < T6C_MAX_BOXES) t6c_box_count++;

    /* ---- channel select: joystick X, edge-triggered (one step per press,
     * not continuous - selecting a channel is a discrete choice, unlike
     * tuning it, which IS continuous below). Wraps 0..2.
     *
     * Requires 2 CONSECUTIVE neutral frames before the next press is
     * armed, not just one: a single-frame blip back through 0 near the
     * digital-joystick deflection threshold could otherwise re-arm and
     * double-fire the select on the very next frame. Genuine releases
     * still register the same way, one frame later. */
    joyx = vxtInJoyX(parm);
    if (joyx == 0) {
        if (joyx_neutral_frames < 255) joyx_neutral_frames++;
    } else {
        if (joyx_neutral_frames >= 2 && joyx_prev == 0) {
            if (joyx > 0) sel_channel = (sel_channel + 1) % 3;
            else          sel_channel = (sel_channel + 2) % 3;   /* -1 mod 3 */
        }
        joyx_neutral_frames = 0;
    }
    joyx_prev = joyx;

    /* ---- frequency tune: joystick Y, CONTINUOUS while held (a rate
     * control, not a discrete choice) - clamped to a sane audible range. */
    joyy = vxtInJoyY(parm);
    if (joyy > 0) {
        int16_t f = (int16_t)(ch_freq[sel_channel] + FREQ_STEP);
        ch_freq[sel_channel] = (f > FREQ_MAX) ? FREQ_MAX : f;
    } else if (joyy < 0) {
        int16_t f = (int16_t)(ch_freq[sel_channel] - FREQ_STEP);
        ch_freq[sel_channel] = (f < FREQ_MIN) ? FREQ_MIN : f;
    }

    /* ---- sound: all 3 tone channels, one mixer call enables all three ---- */
    vxtSoundBegin();
    vxtSoundTone(VXT_CH_A, freqToPeriod(ch_freq[0]), 12);
    vxtSoundTone(VXT_CH_B, freqToPeriod(ch_freq[1]), 12);
    vxtSoundTone(VXT_CH_C, freqToPeriod(ch_freq[2]), 12);
    vxtSoundMixer(VXT_MIX_TONE_A | VXT_MIX_TONE_B | VXT_MIX_TONE_C, 0);
    vxtSoundEnd();

    /* ---- draw: boxes AND text, both via vxt_smart, ONE unified list ---- */
    vxtSmartBegin(VXT_SMART_OFFSET, T6C_TOTAL_RECORDS);

#if T6C_DIAG != 2   /* draw boxes unless in text-only diagnostic mode */
    {
        int row_start_done = -1;
        for (i = 0; i < t6c_box_count; i++) {
            int row = i / BOXES_PER_ROW;
            int col = i % BOXES_PER_ROW;
            uint8_t level = INTENSITY_LEVELS[row % 4];

            if (row != row_start_done) {
                /* vxtSmartRecenter() called immediately after an OPEN
                 * (unclosed) draw run leaks a brief unintended visible
                 * stroke - confirmed on hardware. Every row transition
                 * after the first follows the previous row's last box
                 * (an open draw, its closing edge via vxtSmartCont) -
                 * close it first. row_start_done==-1 only on the very
                 * first row, when nothing has been drawn yet - no close
                 * needed there. */
                if (row_start_done != -1) {
                    vxtSmartMove(0, 0);
                }
                vxtSmartRecenter();
                vxtSmartScale(POS_SCALE);
                vxtSmartMoveBig(GRID_TOP - (int32_t)row * BOX_PITCH_PHYS,
                                GRID_LEFT_PHYS, POS_SCALE);
                vxtSmartScale(BOX_EDGE_DUR);
                row_start_done = row;
            }

            vxtSmartIntensity(level);
            vxtSmartDraw((int8_t)BOX_EDGE_RATE, 0);
            vxtSmartCont(0, (int8_t)BOX_EDGE_RATE);
            vxtSmartCont((int8_t)-BOX_EDGE_RATE, 0);
            vxtSmartCont(0, (int8_t)-BOX_EDGE_RATE);

            if (col != BOXES_PER_ROW - 1) {
                vxtSmartMove(0, (int8_t)(BOX_PITCH_PHYS / BOX_EDGE_DUR));
            }
        }
    }
#endif

    /* ---- draw: CH1=/CH2=/CH3= + LINES= readout, via vxt_smart_text ---- */
#if T6C_DIAG != 1   /* draw text unless in boxes-only diagnostic mode */
    {
        int32_t lines_halfw = vxtSmartTextWidthPhys(11) / 2;
        int32_t lines_y = TEXT_Y + 2600;
        vxtSmartTextSetIntensity(VXT_INTENSITY_MEDIUM);
        vxtSmartTextBegin((int16_t)(lines_y / POS_SCALE), (int16_t)(-lines_halfw / POS_SCALE));
        vxtSmartTextStr("LINES = ");
        vxtSmartTextNumber((int16_t)(t6c_box_count * 4));
    }

    for (i = 0; i < 3; i++) {
        int32_t col_x = -SCREEN_HALF_X + 1200 + (int32_t)i * 9000;
        vxtSmartTextSetIntensity((i == sel_channel) ? VXT_INTENSITY_MAX : VXT_INTENSITY_DIM);
        vxtSmartTextBegin((int16_t)(TEXT_Y / POS_SCALE), (int16_t)(col_x / POS_SCALE));
        vxtSmartTextStr(i == 0 ? "CH1=" : (i == 1 ? "CH2=" : "CH3="));
        vxtSmartTextNumber(ch_freq[i]);
    }
#endif

    vxtSmartEnd();
}
