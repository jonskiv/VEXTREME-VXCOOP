/*
 * vxt_font.h - VXT toolkit: font-table data contract.
 * Copyright (C) 2026 Caelotronics.
 *
 * vxt_smart_text.c is a DRIVER, not a font: it knows how to walk a stroke
 * list and turn it into SmartList move/draw records, but it carries no
 * glyph data of its own. A font is a plain data module - one array of
 * VxtFontGlyph entries, each a character plus up to VXT_FONT_MAX_STROKES
 * straight-line strokes on a fixed 3-wide, 5-tall integer grid
 * (x: 0-2, y: 0-4; curves are approximated with straight segments, same
 * compromise every glyph in every font built against this contract makes).
 *
 * To supply a font: define a static const VxtFontGlyph array (unknown
 * characters are simply absent - vxtSmartTextChar() silently draws
 * nothing for them) in its own .c/.h pair, link that module's .o
 * alongside vxt_smart_text.o, and call vxtSmartTextSetFontTable() once at
 * startup with the table and its entry count. Nothing else in the driver
 * needs to change to add, remove, or entirely replace a font.
 */
#ifndef VXT_FONT_H
#define VXT_FONT_H

#include <stdint.h>

#define VXT_FONT_MAX_STROKES  7

typedef struct { int8_t x1, y1, x2, y2; } VxtFontStroke;

typedef struct {
    char ch;
    uint8_t nstrokes;
    VxtFontStroke strokes[VXT_FONT_MAX_STROKES];
} VxtFontGlyph;

#endif /* VXT_FONT_H */
