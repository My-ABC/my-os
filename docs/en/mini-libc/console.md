# `console.h` — console primitives over `SYS_IO`

Low-level building blocks for interactive line editing: blocking
key reads, screen clear, and cursor control. `stdio`'s
`getchar`/`putchar` sit on top of the same ops; use these when you
need cursor control or raw key input.

## `int con_getchar(void)`

- **Parameters**: none.
- **Returns**: the next key byte, or `-1` when a signal aborted the
  wait (check `errno`/signal disposition).
- **Details**: Blocks until a key arrives. Returns the raw kernel
  byte — control bytes like `0x03` (Ctrl+C marker) are passed
  through; map them yourself if needed.

## `void con_clear(void)`

- **Parameters**: none.
- **Returns**: nothing.
- **Details**: Clears the text screen and homes the cursor via
  `SYS_IO/IO_CLEAR`.

## `int con_cursor_get(uint16_t *row, uint16_t *col)`

- **Parameters**: `row`, `col` — out-params receiving the cursor
  cell (character coordinates).
- **Returns**: `0` on success, `<0` on error.
- **Details**: Wraps `IO_CURSOR_GET`. Note: under QEMU the emulated
  VGA text cursor readback can be unreliable — validate the values
  defensively.

## `int con_cursor_set(uint16_t row, uint16_t col)`

- **Parameters**: `row`, `col` — target cell.
- **Returns**: `0` on success, `<0` on error.
- **Details**: Wraps `IO_CURSOR_SET`; coordinates are in character
  cells, not pixels.

## `int con_size(uint16_t *rows, uint16_t *cols)`

- **Parameters**: `rows`, `cols` — out-params receiving the console
  dimensions in character cells.
- **Returns**: `0` on success, `<0` on error.
- **Details**: Wraps `IO_CONSOLE_INFO`.
