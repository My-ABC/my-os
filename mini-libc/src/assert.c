/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void _Noreturn __assert_fail(const char *expr, const char *file,
                             unsigned line, const char *func) {
    fprintf(stdout,
            "Assertion failed: %s\n"
            "  file: %s\n"
            "  func: %s\n"
            "  line: %u\n",
            expr, file, func, line);
    abort();
}
