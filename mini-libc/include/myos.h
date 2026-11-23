/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_MYOS_H
#define _MINI_LIBC_MYOS_H

/* mini-libc umbrella header: includes every public header so a
 * single `#include <myos.h>` gives access to the whole API.
 * The headers remain usable individually; this file only
 * aggregates them. */

/* ---- freestanding C standard headers ---- */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- MyOS-specific headers ---- */
#include <console.h>
#include <fs.h>
#include <power.h>
#include <process.h>
#include <sys/syscall.h>
#include <sys/types.h>

#endif /* _MINI_LIBC_MYOS_H */
