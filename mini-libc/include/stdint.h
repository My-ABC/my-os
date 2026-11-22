/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_STDINT_H
#define _MINI_LIBC_STDINT_H

/* Fixed-width integer types for i386 (LP32: int/long/pointer = 32-bit). */

typedef signed char int8_t;
typedef short int int16_t;
typedef int int32_t;
typedef long long int64_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef int int_least8_t;
typedef int int_least16_t;
typedef int int_least32_t;
typedef long long int_least64_t;

typedef unsigned int uint_least8_t;
typedef unsigned int uint_least16_t;
typedef unsigned int uint_least32_t;
typedef unsigned long long uint_least64_t;

typedef int int_fast8_t;
typedef int int_fast16_t;
typedef int int_fast32_t;
typedef long long int_fast64_t;

typedef unsigned int uint_fast8_t;
typedef unsigned int uint_fast16_t;
typedef unsigned int uint_fast32_t;
typedef unsigned long long uint_fast64_t;

typedef int intptr_t;
typedef unsigned int uintptr_t;

typedef int intmax_t;
typedef unsigned int uintmax_t;

#define INT8_MAX   127
#define INT8_MIN   (-128)
#define INT16_MAX  32767
#define INT16_MIN  (-32768)
#define INT32_MAX  2147483647
#define INT32_MIN  (-2147483647 - 1)
#define INT64_MAX  9223372036854775807LL
#define INT64_MIN  (-9223372036854775807LL - 1)

#define UINT8_MAX   255U
#define UINT16_MAX  65535U
#define UINT32_MAX  4294967295U
#define UINT64_MAX  18446744073709551615ULL

#define INTPTR_MAX   INT32_MAX
#define INTPTR_MIN   INT32_MIN
#define UINTPTR_MAX  UINT32_MAX

#define INTMAX_MAX   INT32_MAX
#define INTMAX_MIN   INT32_MIN
#define UINTMAX_MAX  UINT32_MAX

#endif /* _MINI_LIBC_STDINT_H */
