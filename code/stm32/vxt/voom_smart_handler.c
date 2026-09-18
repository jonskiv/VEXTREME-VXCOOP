/* ================ VOOM (vxt_smart port) - STM32-side handler ================
 * Copyright (C) 2026 Caelotronics.
 *
 * Runs one frame of Sprite_tm's VOOM (Doom E1M1 wireframe renderer) per RPC
 * and emits the vector list into the served image at VXT_SMART_OFFSET
 * ($0800), where code/app6809/VOOM/VOOM.asm's vxt_smart reads it.
 *
 * The heavy lifting lives in game/VOOM.c (the platform-independent Doom
 * renderer, with its LINEDEFS/SEGS/NODES indexed directly out of flash -
 * see game/VOOM.h/voom_smart.c for why) and game/voom_smart.c (Vectrex
 * output stage, ported from vxt_frame/vxt_draw to vxt_smart). This handler
 * is only the glue: translate input, call the frame.
 *
 * INPUT: vxt_input reports joystick axes as SIGNED bytes; we do VOOM's own
 * digital reduction here (its original sign mapping: +X = RIGHT, +Y = UP).
 * If left/right or up/down feels inverted on hardware, flip the sign tests
 * below - that is a controller-orientation tuning knob, not a logic change.
 * ========================================================================== */

#include <stdint.h>
#include "vxt_input.h"          /* vxtInJoyX/Y, signed-axis accessors      */
#include "../game/VOOM.h"       /* VOOM_KEY_* bit defines                  */
#include "../game/voom_smart.h" /* voomSmartFrame()                        */

/* Map vxt_input's signed axes to VOOM's key bitmask (VOOM's original mapping:
 * positive X -> RIGHT, positive Y -> UP). */
static int voom_smart_keys(volatile uint8_t *parm)
{
    int8_t jx = vxtInJoyX(parm);   /* parm[VXT_IN_JOY1X] == parm[254] */
    int8_t jy = vxtInJoyY(parm);   /* parm[VXT_IN_JOY1Y] == parm[253] */
    int keys = 0;

    if (jx > 0)      keys |= VOOM_KEY_RIGHT;
    else if (jx < 0) keys |= VOOM_KEY_LEFT;

    if (jy > 0)      keys |= VOOM_KEY_UP;
    else if (jy < 0) keys |= VOOM_KEY_DOWN;

    return keys;
}

/* Same handler signature as every other RPC handler, so it slots into the
 * same vxtRpcRegister() dispatch. `parm` is the parmRam block the 6809
 * filled via vxt_input this frame. */
void voom_smart_handler(uint8_t id, volatile uint8_t *parm)
{
    (void)id;
    /* One VOOM frame -> vector list at VXT_SMART_OFFSET (vxt_smart-format).
     * First call loads the resident e1l1 lumps from SD; subsequent calls
     * just render (streaming the rest through voom_smart.c's cache). */
    voomSmartFrame(voom_smart_keys(parm));
}
