#ifndef _STDINT_H
#define _STDINT_H

/* 固定宽度整数类型 */
typedef signed char      int8_t;
typedef unsigned char    uint8_t;
typedef signed short     int16_t;
typedef unsigned short   uint16_t;
typedef signed int       int32_t;
typedef unsigned int     uint32_t;
typedef signed long long int64_t;
typedef unsigned long long uint64_t;

/* 大小类型 */
typedef unsigned int     size_t;
typedef signed int       ssize_t;

/* 指针整数类型 */
typedef unsigned int     uintptr_t;
typedef signed int       intptr_t;

/* 最大宽度整数 */
typedef unsigned long long uintmax_t;
typedef signed long long intmax_t;

/* 宏定义 */
#define INT8_MIN   -128
#define INT16_MIN  -32768
#define INT32_MIN  -2147483648LL
#define INT64_MIN  -9223372036854775808LL

#define INT8_MAX   127
#define INT16_MAX  32767
#define INT32_MAX  2147483647
#define INT64_MAX  9223372036854775807LL

#define UINT8_MAX  255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295U
#define UINT64_MAX 18446744073709551615ULL

#define NULL ((void*)0)

#endif /* _STDINT_H */