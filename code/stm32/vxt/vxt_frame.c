/*
 * vxt_frame.c - VXT toolkit: vector-list writer (STM32 side). GPLv3.
 * Copyright (C) 2026 Caelotronics.
 * See vxt_frame.h for the format and grounding notes.
 *
 * Two rules govern any runtime write into the served cartridge image;
 * both are enforced in one place, vxtImgPut(), rather than left to each
 * caller:
 *
 * 1. The extern declaration of the served buffer must match its real
 *    definition's type exactly. Declaring it as an array when the real
 *    definition is a pointer (or vice versa) compiles cleanly, but an
 *    indexed write then lands at (address of the pointer variable) + pos
 *    instead of inside the buffer - silently corrupting unrelated RAM
 *    while never touching the image the 6809 actually reads.
 *
 * 2. romemu.S inverts the PB6/A15 address bit, so the draw loop's
 *    `clr VIA_port_b` mid-frame flips which of the two 32K banks the
 *    6809's reads map to. A runtime write must therefore be mirrored
 *    into both banks (`pos ^ 0x8000`) - but only for the 64K cartData
 *    buffer; menuData is 20K, and mirroring into it overflows the buffer.
 *
 * vxtImgPut() is non-static so vxt_sound.c can share this same
 * implementation instead of duplicating the mirroring rule.
 */

#include "vxt_frame.h"

/* True definitions live in main.c (lines 67 and 82):
 *   char* romData  = menuData;         <- POINTER, repointed by doStartRom()
 *   char* cartData = c_and_l.cartData; <- 64K cart buffer
 * Do NOT include rom.h here - its `extern char romData[]` array
 * declaration mismatches the real pointer definition (bug #1 above). */
extern char* romData;
extern char* cartData;

static uint16_t vf_pos;
static int      vf_count;
static int      vf_overflow;

void vxtImgPut(uint16_t pos, uint8_t b)
{
    romData[pos] = (char)b;
    if (romData == cartData) {
        /* 64K buffer: mirror into the other 32K bank so the data is
         * visible regardless of PB6 state (bug #2 above). */
        romData[pos ^ 0x8000] = (char)b;
    }
}

void vxtFrameBegin(uint16_t offset)
{
    vf_pos = offset;
    vf_count = 0;
    vf_overflow = 0;
}

int vxtFrameOverflowed(void)
{
    return vf_overflow;
}

void vxtFrameVector(uint8_t intensity, uint8_t scale, uint8_t coord1, uint8_t coord2)
{
    /* Hard bound - see VXT_FRAME_MAX_RECORDS in vxt_frame.h. Overrunning the
     * list into the sound block at $2000 corrupts the PSG command stream and
     * kills sound permanently (observed on hardware). Truncating the picture
     * is a far better failure mode, and vxtFrameOverflowed() makes it
     * detectable rather than baffling. */
    if (vf_count >= VXT_FRAME_MAX_RECORDS) {
        vf_overflow = 1;
        return;
    }
    vf_count++;
    vxtImgPut(vf_pos++, intensity);
    vxtImgPut(vf_pos++, scale);
    vxtImgPut(vf_pos++, coord1);
    vxtImgPut(vf_pos++, coord2);
}

void vxtFrameVectorAt(uint8_t intensity, int recenter,
                      uint8_t scale, int8_t coord1, int8_t coord2)
{
    /* Full 0-127 now that the recenter flag lives on bit 7 (see vxt_frame.h).
     * A recenter record is clamped to 126 so intensity|flag can never form
     * $FF, which ends the list. */
    if (intensity > 127) intensity = 127;
    if (recenter) {
        if (intensity > 126) intensity = 126;
        vxtFrameVector((uint8_t)(intensity | VXT_RECENTER_FLAG),
                       scale, (uint8_t)coord1, (uint8_t)coord2);
    } else {
        vxtFrameVector(intensity, scale, (uint8_t)coord1, (uint8_t)coord2);
    }
}

void vxtFrameMoveBig(int16_t totalY, int16_t totalX, uint8_t scale)
{
    /* Chain small safe moves. A position computed as `start + index*pitch`
     * packed into one int8_t silently wraps once the pitch grows; this
     * cannot. Also the only safe way to emit a move whose rate would exceed
     * 127 (e.g. inter-box pitch at a minimal duration). */
    while (totalY != 0 || totalX != 0) {
        int8_t sy = (int8_t)(totalY > 100 ? 100 : (totalY < -100 ? -100 : totalY));
        int8_t sx = (int8_t)(totalX > 100 ? 100 : (totalX < -100 ? -100 : totalX));
        vxtFrameVectorAt(0, 0, scale, sy, sx);
        totalY = (int16_t)(totalY - sy);
        totalX = (int16_t)(totalX - sx);
    }
}

void vxtFrameEnd(void)
{
    vxtImgPut(vf_pos++, 0xFF);
}
