#ifndef _VBE_H
#define _VBE_H

#include "stdint.h"

int vbe_initialize(uint32_t multiboot_info_addr);
int vbe_blue_screen(void);
int vbe_available(void);
void vbe_set_color(uint8_t foreground, uint8_t background);
void vbe_clear(void);
void vbe_putchar(char c);
void vbe_cursor_tick(void);
void vbe_get_cursor(uint16_t *row, uint16_t *col);
void vbe_set_cursor(uint16_t row, uint16_t col);
uint32_t vbe_columns(void);

#endif