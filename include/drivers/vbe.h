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

#endif