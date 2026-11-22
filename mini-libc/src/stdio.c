/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "sys/syscall.h"

/* ---- printf ---- */

#define OUT_CAP 512
static char out_buf[OUT_CAP];
static size_t out_len;

static void out_flush(void) {
    if (out_len == 0) return;
    out_buf[out_len] = '\0';
    syscall1(SYS_IO, IO_WRITE, (unsigned)out_buf);
    out_len = 0;
}

static void out_ch(char c) {
    if (out_len >= OUT_CAP - 1) out_flush();
    out_buf[out_len++] = c;
}

static void out_str(const char *s) {
    while (*s != '\0') out_ch(*s++);
}

static void out_uint(unsigned v, unsigned base, int upper) {
    char tmp[12];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v != 0);
    while (n > 0) out_ch(tmp[--n]);
}

static void out_int(int v) {
    if (v < 0) {
        out_ch('-');
        out_uint((unsigned)(-(v + 1)) + 1U, 10, 0);
    } else {
        out_uint((unsigned)v, 10, 0);
    }
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p != '\0'; ++p) {
        if (*p != '%') {
            out_ch(*p);
            continue;
        }
        ++p;
        switch (*p) {
            case 'd':
            case 'i':
                out_int(va_arg(ap, int));
                break;
            case 'u':
                out_uint(va_arg(ap, unsigned int), 10, 0);
                break;
            case 'x':
                out_uint(va_arg(ap, unsigned int), 16, 0);
                break;
            case 'X':
                out_uint(va_arg(ap, unsigned int), 16, 1);
                break;
            case 'p':
                out_str("0x");
                out_uint(va_arg(ap, unsigned int), 16, 0);
                break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                out_str(s != 0 ? s : "(null)");
                break;
            }
            case 'c':
                out_ch((char)va_arg(ap, int));
                break;
            case '%':
                out_ch('%');
                break;
            case '\0':
                --p;
                break;
            default:
                out_ch('%');
                out_ch(*p);
                break;
        }
    }
    va_end(ap);
    out_flush();
    return (int)out_len; /* chars flushed in final chunk */
}

/* ---- scanf ---- */

#define IN_CAP 256
static char in_buf[IN_CAP];
static size_t in_pos;
static size_t in_len;
static int in_have;
static int in_pushed = -1;

static int in_getch(void) {
    if (in_pushed >= 0) {
        int c = in_pushed;
        in_pushed = -1;
        return c;
    }
    if (!in_have || in_pos >= in_len) {
        int rc = syscall2(SYS_IO, IO_READ, (unsigned)in_buf, IN_CAP - 1);
        if (rc != 0) return -1;
        in_len = strlen(in_buf);
        in_pos = 0;
        in_have = 1;
        if (in_len == 0) return -1; /* empty line: treat as EOF */
    }
    return (unsigned char)in_buf[in_pos++];
}

static void in_ungetch(int c) {
    if (c >= 0 && in_pushed < 0) in_pushed = c;
}

static int in_skip_ws(void) {
    int c;
    do {
        c = in_getch();
    } while (c >= 0 && isspace(c));
    return c;
}

static int scan_uint(int base, unsigned *out) {
    int c = in_skip_ws();
    unsigned v = 0;
    int any = 0;
    if (base == 0) {
        base = 10;
        if (c == '0') {
            /* peek next char for 0x prefix is overkill; keep decimal */
        }
    }
    while (c >= 0) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned)base + (unsigned)d;
        any = 1;
        c = in_getch();
    }
    in_ungetch(c);
    if (!any) return 0;
    *out = v;
    return 1;
}

int scanf(const char *fmt, ...) {
    va_list ap;
    int assigned = 0;
    int eof_hit = 0;

    va_start(ap, fmt);
    for (const char *p = fmt; *p != '\0'; ++p) {
        if (isspace((unsigned char)*p)) {
            /* literal whitespace in format: skip input whitespace */
            int c = in_skip_ws();
            in_ungetch(c);
            continue;
        }
        if (*p != '%') {
            int c = in_getch();
            if (c < 0) { eof_hit = 1; break; }
            if (c != (unsigned char)*p) {
                in_ungetch(c);
                break;
            }
            continue;
        }
        ++p;
        switch (*p) {
            case 'd':
            case 'i': {
                int c = in_skip_ws();
                int neg = 0;
                if (c == '+' || c == '-') {
                    neg = (c == '-');
                    c = in_getch();
                }
                in_ungetch(c);
                unsigned v;
                if (!scan_uint(10, &v)) goto done;
                int *dst = va_arg(ap, int *);
                *dst = neg ? -(int)v : (int)v;
                ++assigned;
                break;
            }
            case 'u': {
                unsigned v;
                if (!scan_uint(10, &v)) goto done;
                *va_arg(ap, unsigned int *) = v;
                ++assigned;
                break;
            }
            case 'x': {
                unsigned v;
                if (!scan_uint(16, &v)) goto done;
                *va_arg(ap, unsigned int *) = v;
                ++assigned;
                break;
            }
            case 's': {
                char *dst = va_arg(ap, char *);
                int c = in_skip_ws();
                int n = 0;
                while (c >= 0 && !isspace(c)) {
                    dst[n++] = (char)c;
                    c = in_getch();
                }
                in_ungetch(c);
                if (n == 0) goto done;
                dst[n] = '\0';
                ++assigned;
                break;
            }
            case 'c': {
                char *dst = va_arg(ap, char *);
                int c = in_getch();
                if (c < 0) goto done;
                dst[0] = (char)c;
                ++assigned;
                break;
            }
            case '%': {
                int c = in_getch();
                if (c != '%') {
                    in_ungetch(c);
                    goto done;
                }
                break;
            }
            case '\0':
                --p;
                goto done;
            default:
                goto done;
        }
    }
done:
    va_end(ap);
    if (assigned == 0 && eof_hit) return -1;
    if (assigned == 0) {
        /* distinguish "no input yet" from real EOF is impossible here;
         * return EOF only when the input stream signalled it. */
        int c = in_getch();
        if (c < 0) return -1;
        in_ungetch(c);
    }
    return assigned;
}
