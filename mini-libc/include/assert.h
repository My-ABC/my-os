/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_ASSERT_H
#define _MINI_LIBC_ASSERT_H

/* mini-libc assert: on failure prints the expression, file, function,
 * and line to stdout (there is no stderr yet), then abort()s.
 * Define NDEBUG to compile all assertions out. */

#ifdef NDEBUG
#define assert(e) ((void)0)
#else
_Noreturn void __assert_fail(const char *expr, const char *file,
                             unsigned line, const char *func);
#define assert(e) ((e) ? (void)0 : \
    __assert_fail(#e, __FILE__, __LINE__, __func__))
#endif /* NDEBUG */

#endif /* _MINI_LIBC_ASSERT_H */
