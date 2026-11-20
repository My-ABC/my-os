#ifndef _ASCII_FONT_H
#define _ASCII_FONT_H

#include "stdint.h"

#define ASCII_FONT_GLYPH_WIDTH  8
#define ASCII_FONT_GLYPH_HEIGHT 8
#define ASCII_FONT_SCALE        2
#define ASCII_FONT_WIDTH        (ASCII_FONT_GLYPH_WIDTH * ASCII_FONT_SCALE)
#define ASCII_FONT_HEIGHT       (ASCII_FONT_GLYPH_HEIGHT * ASCII_FONT_SCALE)

extern const uint8_t ascii_font[128][ASCII_FONT_GLYPH_HEIGHT];

#endif