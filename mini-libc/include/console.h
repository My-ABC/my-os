/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_CONSOLE_H
#define _MINI_LIBC_CONSOLE_H

#include <stdint.h>

/* mini-libc console primitives over the SYS_IO ABI — the building
 * blocks for interactive line editing (cursor movement, raw key
 * input, screen clear). stdio's getchar/putchar sit on top of the
 * same ops; use these when you need cursor control. */

/* blocking key read; returns the byte or -1 when a signal aborted
 * the wait (check errno/signal disposition) */
int con_getchar(void);

/* clear the text screen and home the cursor */
void con_clear(void);

/* current cursor cell; returns 0 or <0 */
int con_cursor_get(uint16_t *row, uint16_t *col);

/* move the cursor to (row, col) */
int con_cursor_set(uint16_t row, uint16_t col);

/* console size in character cells; returns 0 or <0 */
int con_size(uint16_t *rows, uint16_t *cols);

#endif /* _MINI_LIBC_CONSOLE_H */
