#include "stdio.h"
#include "limits.h"
#include "string.h"
#include "test_support.h"

/* int_to_str/int_to_str_padded negate via `0U - ud` (unsigned), so
 * INT_MIN must render correctly instead of overflowing. */

/* Host stubs for kernel stdio's console/serial/keyboard deps so the
 * file links on the host; output is forwarded to fd 1. */
extern long write(int fd, const void *buf, unsigned long n);
static void host_putc(char c) { write(1, &c, 1); }
void vga_putchar(char c) { host_putc(c); }
void vga_print(const char *s) { while (*s) host_putc(*s++); }
void vga_print_hex(unsigned int v) {
    char b[9]; int i;
    for (i = 7; i >= 0; --i) {
        int d = (int)(v & 0xFU); v >>= 4;
        b[i] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
    }
    b[8] = '\0'; vga_print(b);
}
void vga_print_dec(unsigned int v) {
    char b[12]; int i = 11;
    b[i] = '\0';
    if (v == 0) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + v % 10U); v /= 10U; }
    vga_print(b + i);
}
void serial_print(const char *s) { vga_print(s); }
void serial_putchar(char c) { host_putc(c); }
char keyboard_wait_key(void) { return 0; }

int main(void) {
    char buf[32];

    sprintf(buf, "%d", INT_MIN);
    check(strcmp(buf, "-2147483648") == 0, "sprintf INT_MIN");

    sprintf(buf, "%d", INT_MAX);
    check(strcmp(buf, "2147483647") == 0, "sprintf INT_MAX");

    sprintf(buf, "%d", -42);
    check(strcmp(buf, "-42") == 0, "sprintf negative");

    sprintf(buf, "%05d", -7);
    check(strcmp(buf, "-0007") == 0, "sprintf padded negative");

    return finish_test_suite("int_to_str");
}
