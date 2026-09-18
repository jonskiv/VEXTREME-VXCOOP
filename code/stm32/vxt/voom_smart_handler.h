/*
 * voom_smart_handler.h - STM32-side RPC handler for the vxt_smart port of
 * Copyright (C) 2026 Caelotronics.
 * VOOM (code/app6809/VOOM/VOOM.asm). See voom_smart_handler.c.
 */
#ifndef VOOM_SMART_HANDLER_H
#define VOOM_SMART_HANDLER_H

#include <stdint.h>

void voom_smart_handler(uint8_t id, volatile uint8_t *parm);

#endif /* VOOM_SMART_HANDLER_H */
