/* SPDX-License-Identifier: MIT */
#ifndef _SHELL_RING3_H
#define _SHELL_RING3_H

/* Ring 3 user-mode shell: a port of the kernel shell's portable
 * command set onto the int 0x80 syscall ABI. Runs as a normal ELF32
 * user program (built against mini-libc), launched via `run`. */

int ring3_shell_main(void);

#endif /* _SHELL_RING3_H */
