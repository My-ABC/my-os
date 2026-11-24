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
uint32_t vbe_rows(void);

/* Physical scanout window (virtual address of the mapped framebuffer)
 * and its size in bytes; used by vt.c to save/restore terminal screens. */
uint8_t *vbe_scanout(void);
uint32_t vbe_scanout_bytes(void);

/* Paint a one-line notice into terminal `vt`'s backing store (used
 * when switching to a vt whose shell has not drawn anything yet). */
void vbe_vt_message(int vt, const char *msg);

/* Called by vt.c after a terminal switch: redraw the cursor for the
 * newly visible terminal. */
void vbe_console_switched(void);

#endif