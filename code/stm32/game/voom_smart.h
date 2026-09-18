/*
 * voom_smart.h - VOOM's Vectrex output stage, ported to vxt_smart. See
 * Copyright (C) 2026 Caelotronics.
 * voom_smart.c and code/stm32/voom-vectrex.h (the original vxt_draw
 * version this is a from-scratch parallel of, not a modification of).
 */
#ifndef VOOM_SMART_H
#define VOOM_SMART_H

/* Run one VOOM frame: apply `keys` (VOOM_KEY_* bitmask from VOOM.h), run
 * the renderer, and emit the resulting vector list into the served image
 * at VXT_SMART_OFFSET via vxt_smart. The first call binds VOOM's z-buffer
 * and line list to voom_smart.c's cart-image overlay and points the renderer
 * at the level lumps in flash (game/voom_level_data.S). No caller-supplied
 * buffer, and - unlike an SD-streaming design - no file I/O on any call,
 * first or otherwise: the STM32 side computes every frame on a hard
 * real-time budget while the 6809 waits, so any blocking I/O call here
 * blanks the screen for its duration. */
void voomSmartFrame(int keys);

#endif /* VOOM_SMART_H */
