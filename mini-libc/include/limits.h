/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_LIMITS_H
#define _MINI_LIBC_LIMITS_H

#define CHAR_BIT 8

/* i386 gcc: plain char is signed. */
#define CHAR_MIN (-128)
#define CHAR_MAX 127

#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255

#define SHRT_MIN (-32768)
#define SHRT_MAX 32767
#define USHRT_MAX 65535

#define INT_MIN (-2147483647 - 1)
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U

/* LP32: long is 32-bit. */
#define LONG_MIN (-2147483647L - 1L)
#define LONG_MAX 2147483647L
#define ULONG_MAX 4294967295UL

#define LLONG_MIN (-9223372036854775807LL - 1LL)
#define LLONG_MAX 9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL

#define MB_LEN_MAX 16

#endif /* _MINI_LIBC_LIMITS_H */
