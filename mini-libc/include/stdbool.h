/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_STDBOOL_H
#define _MINI_LIBC_STDBOOL_H

#ifndef __cplusplus
#define bool _Bool
#define true 1
#define false 0
#else
/* C++ has native bool/true/false; nothing to define. */
#endif

#endif /* _MINI_LIBC_STDBOOL_H */
