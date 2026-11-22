/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_POWER_H
#define _MINI_LIBC_POWER_H

/* mini-libc power control over the SYS_POWER ABI.
 *
 * Both calls do not return on success: the kernel performs the ACPI
 * shutdown (S5) or reboot immediately. A negative return means the
 * operation was refused or is unavailable (e.g. no ACPI). */

/* power the machine off (ACPI S5). Returns <0 on failure. */
int power_shutdown(void);

/* reset the machine. Returns <0 on failure. */
int power_reboot(void);

#endif /* _MINI_LIBC_POWER_H */
