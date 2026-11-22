/* SPDX-License-Identifier: MIT */
#include <console.h>
#include <sys/syscall.h>

int con_getchar(void) {
    int c = (int)syscall0(SYS_IO, IO_GETCHAR);
    return c > 0 ? c : -1;
}

void con_clear(void) {
    syscall0(SYS_IO, IO_CLEAR);
}

int con_cursor_get(uint16_t *row, uint16_t *col) {
    if (row == 0 || col == 0) return -1;
    uint32_t rc = syscall0(SYS_IO, IO_CURSOR_GET);
    *row = (uint16_t)(rc >> 16);
    *col = (uint16_t)(rc & 0xFFFF);
    return 0;
}

int con_cursor_set(uint16_t row, uint16_t col) {
    return syscall2(SYS_IO, IO_CURSOR_SET, row, col) == 0 ? 0 : -1;
}

int con_size(uint16_t *rows, uint16_t *cols) {
    if (rows == 0 || cols == 0) return -1;
    uint32_t rc = syscall0(SYS_IO, IO_CONSOLE_INFO);
    *rows = (uint16_t)(rc >> 16);
    *cols = (uint16_t)(rc & 0xFFFF);
    return 0;
}
