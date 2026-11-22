/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_SETJMP_H
#define _MINI_LIBC_SETJMP_H

/* mini-libc setjmp/longjmp over the callee-saved register set.
 *
 * jmp_buf holds ebx/esi/edi/ebp/esp/eip (6 ints). Volatile locals
 * modified between setjmp and longjmp have indeterminate values on
 * the second return — same rule as hosted C. No signal mask is
 * saved (the ABI has no sigprocmask). */

typedef int jmp_buf[6];

int setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif /* _MINI_LIBC_SETJMP_H */
