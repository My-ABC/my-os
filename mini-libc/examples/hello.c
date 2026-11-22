/* SPDX-License-Identifier: MIT */
#include "sys/syscall.h"

static void print_str(const char *s) {
    syscall1(SYS_IO, IO_WRITE, (unsigned)s);
}

int main(int argc, char **argv) {
    char buf[24];
    int pos = 0;
    const char *prefix = "mini-libc hello, argc=";
    while (prefix[pos] != '\0') {
        buf[pos] = prefix[pos];
        ++pos;
    }
    unsigned n = (unsigned)argc;
    char digits[10];
    int d = 0;
    do {
        digits[d++] = (char)('0' + (n % 10U));
        n /= 10U;
    } while (n != 0U);
    while (d > 0) {
        buf[pos++] = digits[--d];
    }
    buf[pos++] = '\n';
    buf[pos] = '\0';
    print_str("mini-libc hello from Ring 3\n");
    print_str(buf);
    return 0;
}
