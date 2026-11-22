/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "sys/syscall.h"

/* ================= console output ================= */

#define CBUF_CAP 512
static char cbuf[CBUF_CAP];
static size_t clen;

static void cflush(void) {
    if (clen == 0) return;
    cbuf[clen] = '\0';
    syscall1(SYS_IO, IO_WRITE, (unsigned)cbuf);
    clen = 0;
}

/* ================= output core ================= */

struct out_sink {
    char *buf;      /* NULL → console */
    size_t cap;     /* string capacity; 0 = unlimited */
    size_t len;     /* chars stored (string mode) */
    int count;      /* chars processed (return value) */
};

static void os_ch(struct out_sink *o, int c) {
    ++o->count;
    if (o->buf == 0) {
        if (clen >= CBUF_CAP - 1) cflush();
        cbuf[clen++] = (char)c;
        return;
    }
    if (o->cap != 0 && o->len + 1 >= o->cap) return;
    o->buf[o->len++] = (char)c;
}

static void os_str(struct out_sink *o, const char *s) {
    while (*s != '\0') os_ch(o, (unsigned char)*s++);
}

struct fspec {
    int left, zero, plus, space, alt;
    int width;      /* -1 = none */
    int prec;       /* -1 = none */
};

static void out_uint_x(struct out_sink *o, unsigned v, unsigned base,
                       int upper, const struct fspec *fs, int neg,
                       const char *prefix) {
    char digits[32];
    int nd = 0;
    const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        digits[nd++] = digs[v % base];
        v /= base;
    } while (v != 0);
    int zeros = 0;
    if (fs->prec > nd) zeros = fs->prec - nd;
    const char *sign = neg ? "-" : (fs->plus ? "+" : (fs->space ? " " : ""));
    int signlen = sign[0] != '\0' ? 1 : 0;
    int prefixlen = prefix != 0 ? (int)strlen(prefix) : 0;
    int body = nd + zeros + prefixlen;
    int width = fs->width;
    int pad = width > signlen + body ? width - signlen - body : 0;
    int zpad = (fs->zero && !fs->left && fs->prec < 0) ? pad : 0;
    int spad = pad - zpad;
    if (!fs->left) while (spad-- > 0) os_ch(o, ' ');
    if (signlen) os_ch(o, *sign);
    if (prefix) os_str(o, prefix);
    while (zpad-- > 0) os_ch(o, '0');
    while (zeros-- > 0) os_ch(o, '0');
    while (nd > 0) os_ch(o, digits[--nd]);
    if (fs->left) while (pad-- > 0) os_ch(o, ' ');
}

static void out_dec(struct out_sink *o, int v, const struct fspec *fs) {
    unsigned mag;
    int neg = 0;
    if (v < 0) { neg = 1; mag = (unsigned)(-(v + 1)) + 1U; }
    else mag = (unsigned)v;
    out_uint_x(o, mag, 10, 0, fs, neg, 0);
}

static void out_str_spec(struct out_sink *o, const char *s,
                         const struct fspec *fs) {
    if (s == 0) s = "(null)";
    int len = 0;
    while (s[len] != '\0') ++len;
    if (fs->prec >= 0 && fs->prec < len) len = fs->prec;
    int pad = fs->width > len ? fs->width - len : 0;
    if (!fs->left) while (pad-- > 0) os_ch(o, ' ');
    for (int i = 0; i < len; ++i) os_ch(o, (unsigned char)s[i]);
    if (fs->left) while (pad-- > 0) os_ch(o, ' ');
}

static void out_char_spec(struct out_sink *o, int c, const struct fspec *fs) {
    int pad = fs->width > 1 ? fs->width - 1 : 0;
    if (!fs->left) while (pad-- > 0) os_ch(o, ' ');
    os_ch(o, c);
    if (fs->left) while (pad-- > 0) os_ch(o, ' ');
}

static int vformat(struct out_sink *o, const char *fmt, va_list ap) {
    for (const char *p = fmt; *p != '\0'; ++p) {
        if (*p != '%') { os_ch(o, (unsigned char)*p); continue; }
        ++p;
        struct fspec fs;
        fs.left = fs.zero = fs.plus = fs.space = fs.alt = 0;
        fs.width = fs.prec = -1;
        while (*p == '-' || *p == '0' || *p == '+' || *p == ' ' || *p == '#') {
            if (*p == '-') fs.left = 1;
            else if (*p == '0') fs.zero = 1;
            else if (*p == '+') fs.plus = 1;
            else if (*p == ' ') fs.space = 1;
            else fs.alt = 1;
            ++p;
        }
        if (*p == '*') { fs.width = va_arg(ap, int); ++p; }
        else {
            int w = 0, any = 0;
            while (*p >= '0' && *p <= '9') { w = w * 10 + (*p - '0'); any = 1; ++p; }
            if (any) fs.width = w;
        }
        if (*p == '.') {
            ++p;
            if (*p == '*') { fs.prec = va_arg(ap, int); ++p; }
            else {
                int pr = 0;
                while (*p >= '0' && *p <= '9') { pr = pr * 10 + (*p - '0'); ++p; }
                fs.prec = pr;
            }
        }
        while (*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' || *p == 't' || *p == 'L')
            ++p;
        switch (*p) {
            case 'd': case 'i':
                out_dec(o, va_arg(ap, int), &fs);
                break;
            case 'u':
                out_uint_x(o, va_arg(ap, unsigned int), 10, 0, &fs, 0, 0);
                break;
            case 'o': {
                unsigned v = va_arg(ap, unsigned int);
                out_uint_x(o, v, 8, 0, &fs, 0, fs.alt && v ? "0" : 0);
                break;
            }
            case 'x': case 'X': {
                unsigned v = va_arg(ap, unsigned int);
                int up = (*p == 'X');
                out_uint_x(o, v, 16, up, &fs, 0,
                           fs.alt && v ? (up ? "0X" : "0x") : 0);
                break;
            }
            case 'p': {
                struct fspec pf = fs;
                pf.width = -1;
                out_uint_x(o, va_arg(ap, unsigned int), 16, 0, &pf, 0, "0x");
                break;
            }
            case 'c':
                out_char_spec(o, va_arg(ap, int), &fs);
                break;
            case 's':
                out_str_spec(o, va_arg(ap, const char *), &fs);
                break;
            case '%':
                os_ch(o, '%');
                break;
            case '\0':
                --p;
                break;
            default:
                os_ch(o, '%');
                os_ch(o, (unsigned char)*p);
                break;
        }
    }
    return o->count;
}

int printf(const char *fmt, ...) {
    struct out_sink o = { 0, 0, 0, 0 };
    va_list ap;
    va_start(ap, fmt);
    int rc = vformat(&o, fmt, ap);
    va_end(ap);
    cflush();
    return rc;
}

int sprintf(char *buf, const char *fmt, ...) {
    struct out_sink o = { buf, 0, 0, 0 };
    va_list ap;
    va_start(ap, fmt);
    int rc = vformat(&o, fmt, ap);
    va_end(ap);
    buf[o.len] = '\0';
    return rc;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    struct out_sink o = { buf, n, 0, 0 };
    va_list ap;
    va_start(ap, fmt);
    int rc = vformat(&o, fmt, ap);
    va_end(ap);
    if (n != 0) buf[o.len] = '\0';
    return rc;
}

int putchar(int c) {
    struct out_sink o = { 0, 0, 0, 0 };
    os_ch(&o, c);
    cflush();
    return c;
}

int puts(const char *s) {
    int rc = printf("%s\n", s != 0 ? s : "(null)");
    return rc < 0 ? EOF : 1;
}

/* ================= console input ================= */

#define IN_CAP 256
static char in_buf[IN_CAP];
static size_t in_pos, in_len;
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

/* ================= input core ================= */

struct in_src {
    const char *s;      /* NULL → console */
    size_t pos;
    int pushed;
};

static int isrc_getc(struct in_src *src) {
    if (src->pushed >= 0) {
        int c = src->pushed;
        src->pushed = -1;
        return c;
    }
    if (src->s == 0) return in_getch();
    unsigned char c = (unsigned char)src->s[src->pos];
    if (c == 0) return -1;
    ++src->pos;
    return c;
}

static void isrc_ungetc(struct in_src *src, int c) {
    if (c < 0) return;
    if (src->s == 0) { in_ungetch(c); return; }
    if (src->pushed < 0) src->pushed = c;
}

static int isrc_skip_ws(struct in_src *src) {
    int c;
    do {
        c = isrc_getc(src);
    } while (c >= 0 && isspace(c));
    return c;
}

struct ispec {
    int suppress;
    int width;          /* -1 = none */
};

static int scan_uint(struct in_src *src, int base, struct ispec *sp,
                     unsigned *out) {
    int c = isrc_skip_ws(src);
    /* base 0 (%i): auto-detect 0x→16, leading 0→8, else 10.
     * base 16 (%x/%p): accept an optional 0x prefix. */
    if (c == '0' && (base == 0 || base == 16)) {
        int c2 = isrc_getc(src);
        if (c2 == 'x' || c2 == 'X') {
            base = 16;
            c = isrc_getc(src);
        } else {
            isrc_ungetc(src, c2);
            if (base == 0) {
                base = (c2 >= '0' && c2 <= '7') ? 8 : 10;
            }
        }
    } else if (base == 0) {
        base = 10;
    }
    unsigned v = 0;
    int any = 0, digits = 0;
    while (c >= 0) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        if (sp->width >= 0 && digits >= sp->width) break;
        v = v * (unsigned)base + (unsigned)d;
        any = 1;
        ++digits;
        c = isrc_getc(src);
    }
    isrc_ungetc(src, c);
    if (!any) return 0;
    *out = v;
    return 1;
}

static int vfscanf(struct in_src *src, const char *fmt, va_list ap) {
    int assigned = 0;
    int eof_hit = 0;
    int matched = 0;

    for (const char *p = fmt; *p != '\0'; ++p) {
        if (isspace((unsigned char)*p)) {
            int c = isrc_skip_ws(src);
            isrc_ungetc(src, c);
            continue;
        }
        if (*p != '%') {
            int c = isrc_getc(src);
            if (c < 0) { eof_hit = 1; break; }
            if (c != (unsigned char)*p) {
                isrc_ungetc(src, c);
                break;
            }
            ++matched;
            continue;
        }
        ++p;
        struct ispec sp;
        sp.suppress = 0;
        sp.width = -1;
        if (*p == '*') { sp.suppress = 1; ++p; }
        if (*p >= '0' && *p <= '9') {
            int w = 0;
            while (*p >= '0' && *p <= '9') { w = w * 10 + (*p - '0'); ++p; }
            sp.width = w;
        }
        while (*p == 'h' || *p == 'l') ++p;
        switch (*p) {
            case 'd': case 'i': {
                int neg = 0;
                {
                    int c = isrc_skip_ws(src);
                    if (c == '+' || c == '-') {
                        neg = (c == '-');
                        c = isrc_getc(src);
                    }
                    isrc_ungetc(src, c);
                }
                unsigned v;
                if (!scan_uint(src, *p == 'i' ? 0 : 10, &sp, &v)) goto done;
                if (!sp.suppress) {
                    int *dst = va_arg(ap, int *);
                    *dst = neg ? -(int)v : (int)v;
                    ++assigned;
                }
                ++matched;
                break;
            }
            case 'u': {
                unsigned v;
                if (!scan_uint(src, 10, &sp, &v)) goto done;
                if (!sp.suppress) {
                    *va_arg(ap, unsigned int *) = v;
                    ++assigned;
                }
                ++matched;
                break;
            }
            case 'o': {
                unsigned v;
                if (!scan_uint(src, 8, &sp, &v)) goto done;
                if (!sp.suppress) {
                    *va_arg(ap, unsigned int *) = v;
                    ++assigned;
                }
                ++matched;
                break;
            }
            case 'x': case 'X': case 'p': {
                unsigned v;
                if (!scan_uint(src, 16, &sp, &v)) goto done;
                if (!sp.suppress) {
                    *va_arg(ap, unsigned int *) = v;
                    ++assigned;
                }
                ++matched;
                break;
            }
            case 's': {
                char *dst = sp.suppress ? 0 : va_arg(ap, char *);
                int c = isrc_skip_ws(src);
                int n = 0, lim = sp.width >= 0 ? sp.width : 1 << 28;
                while (c >= 0 && !isspace(c) && n < lim) {
                    if (!sp.suppress) dst[n] = (char)c;
                    ++n;
                    c = isrc_getc(src);
                }
                isrc_ungetc(src, c);
                if (n == 0) goto done;
                if (!sp.suppress) dst[n] = '\0';
                ++matched;
                if (!sp.suppress) ++assigned;
                break;
            }
            case 'c': {
                char *dst = sp.suppress ? 0 : va_arg(ap, char *);
                int lim = sp.width >= 0 ? sp.width : 1;
                int n = 0;
                while (n < lim) {
                    int c = isrc_getc(src);
                    if (c < 0) goto done;
                    if (!sp.suppress) dst[n] = (char)c;
                    ++n;
                }
                ++matched;
                if (!sp.suppress) ++assigned;
                break;
            }
            case '%': {
                int c = isrc_getc(src);
                if (c != '%') {
                    isrc_ungetc(src, c);
                    goto done;
                }
                ++matched;
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
    if (assigned == 0 && matched == 0 && eof_hit) return -1;
    return assigned;
}

int getchar(void) {
    return in_getch();
}

int scanf(const char *fmt, ...) {
    struct in_src src = { 0, 0, -1 };
    va_list ap;
    va_start(ap, fmt);
    int rc = vfscanf(&src, fmt, ap);
    va_end(ap);
    return rc;
}

int sscanf(const char *s, const char *fmt, ...) {
    struct in_src src = { s, 0, -1 };
    va_list ap;
    va_start(ap, fmt);
    int rc = vfscanf(&src, fmt, ap);
    va_end(ap);
    return rc;
}
