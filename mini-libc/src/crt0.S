/* SPDX-License-Identifier: MIT */
/* crt0: ELF entry point for mini-libc programs.
 *
 * The kernel hands control to _start with an empty user stack
 * (esp = user_stack_top). This stub fabricates a minimal
 * argc/argv/envp frame, calls main, then exits through
 * SYS_PROCESS/PROC_EXIT so the kernel can reap the process.
 *
 * ABI: eax = class, ebx = sub-op, ecx/edx/esi = args, int 0x80. */

    .intel_syntax noprefix
    .text
    .globl _start
    .extern main

_start:
    and esp, -16
    sub esp, 16
    mov dword ptr [esp], 1              /* argc */
    mov dword ptr [esp + 4], offset argv
    mov dword ptr [esp + 8], 0          /* envp */
    call main
    add esp, 16

    mov ecx, eax                        /* exit status */
    mov eax, 5                          /* SYS_PROCESS */
    mov ebx, 2                          /* PROC_EXIT */
    int 0x80
.hang:
    jmp .hang

    .data
    .align 4
argv:
    .long prog_name
    .long 0
prog_name:
    .asciz "mini-libc"
