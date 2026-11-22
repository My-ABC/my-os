# `console.h`

Console primitives over `SYS_IO` — the building blocks for
interactive line editing (cursor movement, raw key input, clear).

`int con_getchar(void)` — blocking key read; byte or `-1` when a
signal aborted the wait.
`void con_clear(void)` — clear text screen, home cursor.
`int con_cursor_get(uint16_t *row, uint16_t *col)` — current cell.
`int con_cursor_set(uint16_t row, uint16_t col)` — move cursor.

Note: the emulated VGA text cursor may not round-trip reads
reliably under QEMU; verify positions defensively.
